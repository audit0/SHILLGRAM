/*
ShillGramm: SHILLVPN built into the app.
*/
#include "shillgramm/shill_vpn.h"

#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "lang/lang_instance.h"
#include "settings.h"
#include "shillgramm/shill_snooze.h" // Tr
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/basic_click_handlers.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

namespace Shill {
namespace {

// Where the subscription is served when the pasted link does not say it.
constexpr auto kSite = "https://shillvpn.site";
constexpr auto kBot = "SHILLVPN_bot";
constexpr auto kUserPrefix = "shill";
constexpr auto kPortAttempts = 50; // x 150 ms: the core has 7.5 s to listen.
constexpr auto kMaxRestarts = 5;
constexpr auto kRefreshInterval = crl::time(6 * 3600 * 1000);
constexpr auto kFetchTimeout = 15 * 1000;

const auto kTokenRe = QRegularExpression(
	u"(?:^|/)([0-9a-f]{24}\\.[0-9a-f]{64})(?:$|[/?#])"_q);

[[nodiscard]] QString FlagsPath() {
	return cWorkingDir() + u"tdata/shillgramm_vpn.json"_q;
}

[[nodiscard]] QString RandomString(int length) {
	static const auto chars = u"abcdefghijkmnpqrstuvwxyz23456789"_q;
	auto result = QString();
	result.reserve(length);
	auto *generator = QRandomGenerator::system();
	for (auto i = 0; i != length; ++i) {
		result.append(chars[generator->bounded(int(chars.size()))]);
	}
	return result;
}

[[nodiscard]] int FreePort() {
	auto server = QTcpServer();
	if (!server.listen(QHostAddress::LocalHost, 0)) {
		return 0;
	}
	const auto result = int(server.serverPort());
	server.close();
	return result;
}

[[nodiscard]] QString DaysText(int days) {
	if (!Lang::GetInstance().id().startsWith(u"ru"_q)) {
		return QString::number(days) + (days == 1 ? u" day"_q : u" days"_q);
	}
	const auto mod10 = days % 10;
	const auto mod100 = days % 100;
	const auto word = (mod10 == 1 && mod100 != 11)
		? u"день"_q
		: (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
		? u"дня"_q
		: u"дней"_q;
	return QString::number(days) + ' ' + word;
}

[[nodiscard]] bool IsOurProxy(const MTP::ProxyData &proxy) {
	return (proxy.type == MTP::ProxyData::Type::Socks5)
		&& (proxy.host == u"127.0.0.1"_q)
		&& proxy.user.startsWith(QString::fromLatin1(kUserPrefix));
}

// Hysteria 2 (QUIC): what works best on Russian mobile networks.
// Xray 26.9 names it "hysteria", version 2 (infra/conf/hysteria.go and
// transport_method.go of v26.9.9); "insecure" is gone from Xray, only a
// pinned certificate replaces the name check.
[[nodiscard]] std::optional<QJsonObject> HysteriaOutbound(
		const QUrl &url,
		const QString &tag) {
	const auto host = url.host();
	const auto port = url.port();
	const auto auth = url.userInfo(QUrl::FullyDecoded);
	if (host.isEmpty() || host == u"0.0.0.0"_q || port <= 0 || auth.isEmpty()) {
		return std::nullopt;
	}
	const auto query = QUrlQuery(url);
	const auto value = [&](const QString &key) {
		return query.queryItemValue(key, QUrl::FullyDecoded);
	};
	if (!value(u"obfs"_q).isEmpty()) {
		return std::nullopt; // Salamander is not in the bundled core.
	}
	auto tls = QJsonObject{
		{ u"serverName"_q, value(u"sni"_q).isEmpty() ? host : value(u"sni"_q) },
		{ u"alpn"_q, QJsonArray{ u"h3"_q } },
	};
	const auto pin = value(u"pinSHA256"_q);
	if (!pin.isEmpty()) {
		tls.insert(u"pinnedPeerCertSha256"_q, pin);
	} else if (value(u"insecure"_q) == u"1"_q) {
		return std::nullopt;
	}
	return QJsonObject{
		{ u"tag"_q, tag },
		{ u"protocol"_q, u"hysteria"_q },
		{ u"settings"_q, QJsonObject{
			{ u"version"_q, 2 },
			{ u"address"_q, host },
			{ u"port"_q, port },
		} },
		{ u"streamSettings"_q, QJsonObject{
			{ u"network"_q, u"hysteria"_q },
			{ u"security"_q, u"tls"_q },
			{ u"tlsSettings"_q, tls },
			{ u"hysteriaSettings"_q, QJsonObject{
				{ u"version"_q, 2 },
				{ u"auth"_q, auth },
			} },
		} },
	};
}

// One share link as an Xray outbound; nullopt for the info lines of the
// list and for what the bundled core does not open.
[[nodiscard]] std::optional<QJsonObject> Outbound(
		const QString &uri,
		const QString &tag) {
	const auto url = QUrl(uri);
	if (!url.isValid()) {
		return std::nullopt;
	} else if (url.scheme() == u"hysteria2"_q || url.scheme() == u"hy2"_q) {
		return HysteriaOutbound(url, tag);
	} else if (url.scheme() != u"vless"_q) {
		return std::nullopt;
	}
	const auto host = url.host();
	const auto port = url.port();
	const auto id = url.userName(QUrl::FullyDecoded);
	if (host.isEmpty()
		|| host == u"0.0.0.0"_q // Info lines: "3 дня осталось" and such.
		|| port <= 0
		|| id.size() != 36) {
		return std::nullopt;
	}
	const auto query = QUrlQuery(url);
	const auto value = [&](const QString &key) {
		return query.queryItemValue(key, QUrl::FullyDecoded);
	};
	const auto security = value(u"security"_q);
	const auto network = value(u"type"_q).isEmpty()
		? u"tcp"_q
		: value(u"type"_q);
	if (network != u"tcp"_q && network != u"xhttp"_q) {
		return std::nullopt;
	}
	auto user = QJsonObject{
		{ u"id"_q, id },
		{ u"encryption"_q, u"none"_q },
	};
	if (!value(u"flow"_q).isEmpty()) {
		user.insert(u"flow"_q, value(u"flow"_q));
	}
	auto stream = QJsonObject{ { u"network"_q, network } };
	const auto fingerprint = value(u"fp"_q).isEmpty()
		? u"chrome"_q
		: value(u"fp"_q);
	if (security == u"reality"_q) {
		stream.insert(u"security"_q, u"reality"_q);
		stream.insert(u"realitySettings"_q, QJsonObject{
			{ u"serverName"_q, value(u"sni"_q) },
			{ u"fingerprint"_q, fingerprint },
			{ u"publicKey"_q, value(u"pbk"_q) },
			{ u"shortId"_q, value(u"sid"_q) },
		});
	} else if (security == u"tls"_q) {
		stream.insert(u"security"_q, u"tls"_q);
		stream.insert(u"tlsSettings"_q, QJsonObject{
			{ u"serverName"_q, value(u"sni"_q).isEmpty()
				? host
				: value(u"sni"_q) },
			{ u"fingerprint"_q, fingerprint },
		});
	} else {
		return std::nullopt; // Plain VLESS is not something we serve.
	}
	if (network == u"xhttp"_q) {
		auto xhttp = QJsonObject();
		if (!value(u"path"_q).isEmpty()) {
			xhttp.insert(u"path"_q, value(u"path"_q));
		}
		if (!value(u"host"_q).isEmpty()) {
			xhttp.insert(u"host"_q, value(u"host"_q));
		}
		if (!value(u"mode"_q).isEmpty()) {
			xhttp.insert(u"mode"_q, value(u"mode"_q));
		}
		const auto extra = QJsonDocument::fromJson(
			value(u"extra"_q).toUtf8());
		if (extra.isObject()) {
			xhttp.insert(u"extra"_q, extra.object());
		}
		stream.insert(u"xhttpSettings"_q, xhttp);
	}
	return QJsonObject{
		{ u"tag"_q, tag },
		{ u"protocol"_q, u"vless"_q },
		{ u"settings"_q, QJsonObject{
			{ u"vnext"_q, QJsonArray{ QJsonObject{
				{ u"address"_q, host },
				{ u"port"_q, port },
				{ u"users"_q, QJsonArray{ user } },
			} } },
		} },
		{ u"streamSettings"_q, stream },
	};
}

} // namespace

Vpn &Vpn::Instance() {
	static auto instance = new Vpn(); // Lives until exit, stop() cleans up.
	return *instance;
}

Vpn::Vpn()
: _restartTimer([=] { launch(); })
, _refreshTimer([=] { refresh(); }) {
	loadFlags();
	loadSecrets();
	_state = _token.isEmpty() ? VpnState::None : VpnState::Off;
}

std::optional<QString> Vpn::TokenFromLink(const QString &link) {
	const auto match = kTokenRe.match(link.trimmed());
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	return match.captured(1);
}

void Vpn::start() {
	if (_enabled && !_token.isEmpty()) {
		if (!_entries.empty()) {
			launch(); // From the kept list: works when the site is slow.
		}
		refresh();
	} else if (IsOurProxy(Core::App().settings().proxy().selected())) {
		useProxy(false); // The core is not coming: do not wait on its port.
	}
}

void Vpn::stop() {
	_stopping = true;
	_restartTimer.cancel();
	_refreshTimer.cancel();
	if (const auto core = _core.data()) {
		core->disconnect();
		core->terminate();
		if (!core->waitForFinished(1500)) {
			core->kill();
			core->waitForFinished(500);
		}
		delete core;
	}
}

bool Vpn::hasSubscription() const {
	return !_token.isEmpty();
}

bool Vpn::enabled() const {
	return _enabled;
}

VpnState Vpn::state() const {
	return _state.current();
}

rpl::producer<VpnState> Vpn::stateValue() const {
	return _state.value();
}

QString Vpn::error() const {
	return _error;
}

TimeId Vpn::expiresAt() const {
	return _expiresAt;
}

QString Vpn::statusText() const {
	const auto now = base::unixtime::now();
	const auto left = (_expiresAt > now)
		? DaysText(int((_expiresAt - now + 86399) / 86400))
		: QString();
	switch (_state.current()) {
	case VpnState::None:
		return Tr("Not connected", "Не подключён");
	case VpnState::Off:
		return Tr("Off", "Выключен")
			+ (left.isEmpty() ? QString() : u" · "_q + left);
	case VpnState::Loading:
		return Tr("Loading the subscription…", "Загружаю подписку…");
	case VpnState::Starting:
		return Tr("Connecting…", "Подключаю…");
	case VpnState::On:
		if (_expiresAt && _expiresAt <= now) {
			return Tr("Access ended", "Доступ закончился");
		}
		return Tr("On", "Включён")
			+ (left.isEmpty() ? QString() : u" · "_q + left);
	case VpnState::Error:
		return _error;
	}
	return QString();
}

rpl::producer<QString> Vpn::menuText() const {
	return _state.value() | rpl::map([=](VpnState state) {
		const auto now = base::unixtime::now();
		if (state == VpnState::None) {
			return u"SHILLVPN"_q;
		} else if (_expiresAt > now) {
			return u"SHILLVPN · "_q
				+ DaysText(int((_expiresAt - now + 86399) / 86400));
		} else if (_expiresAt) {
			return u"SHILLVPN · "_q + Tr("renew", "продлить");
		}
		return u"SHILLVPN"_q;
	});
}

QString Vpn::subscriptionUrl() const {
	const auto url = QUrl(_link.trimmed());
	if (url.scheme() == u"https"_q
		&& url.path().contains(u"/sub/"_q)
		&& url.path().endsWith(_token)) {
		return url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
	}
	const auto origin = (url.scheme() == u"https"_q && !url.host().isEmpty())
		? (u"https://"_q + url.host())
		: QString::fromLatin1(kSite);
	return origin + u"/sub/"_q + _token;
}

QString Vpn::connectPageUrl() const {
	return _token.isEmpty()
		? QString::fromLatin1(kSite) + u"/app/buy/"_q
		: QString::fromLatin1(kSite) + u"/c/"_q + _token;
}

void Vpn::setLink(const QString &link, Fn<void(QString)> done) {
	const auto token = TokenFromLink(link);
	if (!token) {
		done(Tr(
			"This is not a SHILLVPN subscription link.",
			"Это не ссылка подписки SHILLVPN."));
		return;
	}
	const auto wasLink = _link;
	const auto wasToken = _token;
	_link = link.trimmed();
	_token = *token;
	setState(VpnState::Loading);
	fetch([=](QString error) {
		if (!error.isEmpty()) {
			_link = wasLink;
			_token = wasToken;
			setState(_token.isEmpty() ? VpnState::None : VpnState::Off);
			done(error);
			return;
		}
		saveSecrets();
		_enabled = true;
		saveFlags();
		_restarts = 0;
		launch();
		done(QString());
	});
}

void Vpn::setEnabled(bool enabled) {
	if (_token.isEmpty() || _enabled == enabled) {
		return;
	}
	_enabled = enabled;
	saveFlags();
	if (enabled) {
		_restarts = 0;
		if (_entries.empty()) {
			refresh();
		} else {
			launch();
		}
	} else {
		_restartTimer.cancel();
		_stopping = true;
		if (const auto core = _core.data()) {
			core->disconnect();
			core->terminate();
			core->deleteLater();
		}
		_stopping = false;
		useProxy(false);
		setState(VpnState::Off);
	}
}

void Vpn::refresh() {
	if (_token.isEmpty()) {
		return;
	}
	_refreshTimer.callOnce(kRefreshInterval);
	const auto was = _body;
	if (_state.current() != VpnState::On) {
		setState(VpnState::Loading);
	}
	fetch([=](QString error) {
		if (!error.isEmpty()) {
			if (_state.current() == VpnState::Loading) {
				setState(_entries.empty() ? VpnState::Error : VpnState::Off,
					error);
				if (_enabled && !_entries.empty()) {
					launch();
				}
			}
			return;
		}
		saveSecrets();
		if (!_enabled) {
			setState(VpnState::Off);
		} else if (_body != was || !_core) {
			launch();
		} else {
			setState(VpnState::On); // Same list: refreshes the days left.
		}
	});
}

void Vpn::forget() {
	setEnabled(false);
	_link = QString();
	_token = QString();
	_body = QByteArray();
	_entries.clear();
	_expiresAt = 0;
	KeychainRemove(u"link"_q);
	KeychainRemove(u"body"_q);
	saveFlags();
	setState(VpnState::None);
}

void Vpn::fetch(Fn<void(QString)> done) {
	if (!_network) {
		_network = new QNetworkAccessManager();
		// Straight to the site: the app-wide proxy may be our own port.
		_network->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	}
	const auto viaCore = std::make_shared<bool>(false);
	const auto request = [=](auto &&self) -> void {
		auto request = QNetworkRequest(QUrl(subscriptionUrl()));
		request.setHeader(
			QNetworkRequest::UserAgentHeader,
			u"ShillGramm/1.0"_q);
		request.setTransferTimeout(kFetchTimeout);
		const auto reply = _network->get(request);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			reply->deleteLater();
			const auto code = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (reply->error() == QNetworkReply::NoError && code == 200) {
				const auto info = QString::fromLatin1(
					reply->rawHeader("subscription-userinfo"));
				const auto expire = QRegularExpression(
					u"expire=(\\d+)"_q).match(info);
				if (!applyBody(reply->readAll())) {
					done(Tr(
						"The subscription has no servers this app can use.",
						"В подписке нет серверов, которые открывает приложение."));
					return;
				}
				_expiresAt = expire.hasMatch()
					? TimeId(expire.captured(1).toLongLong())
					: 0;
				saveFlags();
				done(QString());
			} else if (code == 404 || code == 403) {
				done(Tr(
					"The link is not valid anymore. Take a new one in "
					"@SHILLVPN_bot or in the cabinet on the site.",
					"Ссылка больше не действует. Возьмите новую в "
					"@SHILLVPN_bot или в кабинете на сайте."));
			} else if (!*viaCore && _core && _port) {
				// The site is out of reach directly: try through the tunnel.
				*viaCore = true;
				_network->setProxy(QNetworkProxy(
					QNetworkProxy::Socks5Proxy,
					u"127.0.0.1"_q,
					quint16(_port),
					_user,
					_password));
				self(self);
			} else {
				done(Tr(
					"Could not reach shillvpn.site. Check the internet "
					"and try again.",
					"Не удалось связаться с shillvpn.site. Проверьте "
					"интернет и попробуйте ещё раз."));
			}
		});
	};
	_network->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	request(request);
}

bool Vpn::applyBody(const QByteArray &body) {
	auto text = body.trimmed();
	if (!text.contains("://")) {
		const auto compact = QByteArray(text).replace('\n', "").replace('\r', "");
		text = QByteArray::fromBase64(compact, QByteArray::Base64Encoding);
		if (!text.contains("://")) {
			text = QByteArray::fromBase64(compact, QByteArray::Base64UrlEncoding);
		}
	}
	auto entries = std::vector<Entry>();
	for (const auto &line : QString::fromUtf8(text).split('\n')) {
		const auto uri = line.trimmed();
		if (Outbound(uri, u"p"_q)) {
			entries.push_back({
				.title = QUrl(uri).fragment(QUrl::FullyDecoded),
				.uri = uri,
			});
		}
	}
	if (entries.empty()) {
		return false;
	}
	_body = body;
	_entries = std::move(entries);
	return true;
}

QByteArray Vpn::buildConfig() const {
	auto outbounds = QJsonArray();
	auto index = 0;
	for (const auto &entry : _entries) {
		if (const auto outbound = Outbound(
				entry.uri,
				u"p%1"_q.arg(index))) {
			outbounds.append(*outbound);
			++index;
		}
	}
	auto config = QJsonObject{
		// No access log: SHILLVPN keeps no history of connections.
		{ u"log"_q, QJsonObject{
			{ u"loglevel"_q, u"warning"_q },
			{ u"access"_q, u"none"_q },
		} },
		{ u"inbounds"_q, QJsonArray{ QJsonObject{
			{ u"tag"_q, u"telegram"_q },
			{ u"listen"_q, u"127.0.0.1"_q },
			{ u"port"_q, _port },
			{ u"protocol"_q, u"socks"_q },
			{ u"settings"_q, QJsonObject{
				{ u"auth"_q, u"password"_q },
				{ u"accounts"_q, QJsonArray{ QJsonObject{
					{ u"user"_q, _user },
					{ u"pass"_q, _password },
				} } },
				{ u"udp"_q, false },
			} },
		} } },
		{ u"outbounds"_q, outbounds },
	};
	if (index > 1) {
		// Several entries: the core probes them and takes the quickest
		// one that answers, so a cut protocol does not stop Telegram.
		config.insert(u"observatory"_q, QJsonObject{
			{ u"subjectSelector"_q, QJsonArray{ u"p"_q } },
			{ u"probeURL"_q, u"https://www.gstatic.com/generate_204"_q },
			{ u"probeInterval"_q, u"2m"_q },
			{ u"enableConcurrency"_q, true },
		});
		config.insert(u"routing"_q, QJsonObject{
			{ u"balancers"_q, QJsonArray{ QJsonObject{
				{ u"tag"_q, u"auto"_q },
				{ u"selector"_q, QJsonArray{ u"p"_q } },
				{ u"strategy"_q, QJsonObject{
					{ u"type"_q, u"leastPing"_q },
				} },
				{ u"fallbackTag"_q, u"p0"_q },
			} } },
			{ u"rules"_q, QJsonArray{ QJsonObject{
				{ u"type"_q, u"field"_q },
				{ u"inboundTag"_q, QJsonArray{ u"telegram"_q } },
				{ u"balancerTag"_q, u"auto"_q },
			} } },
		});
	}
	return QJsonDocument(config).toJson(QJsonDocument::Compact);
}

QString Vpn::corePath() const {
	const auto dir = QDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
	const auto name = u"xray.exe"_q;
#else // Q_OS_WIN
	const auto name = u"xray"_q;
#endif // Q_OS_WIN
	for (const auto &candidate : {
		dir.filePath(u"../Helpers/"_q + name), // macOS bundle.
		dir.filePath(name),
		dir.filePath(u"../Resources/"_q + name),
	}) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return QString();
}

void Vpn::launch() {
	if (_stopping || !_enabled || _entries.empty()) {
		return;
	}
	const auto path = corePath();
	if (path.isEmpty()) {
		setState(VpnState::Error, Tr(
			"The VPN core is missing from this build.",
			"В этой сборке нет ядра VPN."));
		return;
	}
	if (const auto core = _core.data()) {
		core->disconnect();
		core->terminate();
		core->waitForFinished(1000);
		delete core;
	}
	_port = FreePort();
	if (!_port) {
		setState(VpnState::Error, Tr(
			"No free local port for the VPN.",
			"Нет свободного локального порта для VPN."));
		return;
	}
	_user = QString::fromLatin1(kUserPrefix) + RandomString(8);
	_password = RandomString(24);
	setState(VpnState::Starting);

	const auto core = new QProcess();
	_core = core;
	core->setProcessChannelMode(QProcess::MergedChannels);
	// Its lines may name servers: read and drop, never logged.
	QObject::connect(core, &QProcess::readyRead, [=] {
		core->readAll();
	});
	QObject::connect(core, &QProcess::started, [=] {
		core->write(buildConfig());
		core->closeWriteChannel();
		waitForPort(0);
	});
	QObject::connect(
		core,
		&QProcess::finished,
		[=](int, QProcess::ExitStatus) {
			if (_core == core) {
				coreFinished();
			}
			core->deleteLater();
		});
	QObject::connect(core, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart && _core == core) {
			coreFinished();
			core->deleteLater();
		}
	});
	core->start(path, { u"run"_q, u"-config"_q, u"stdin:"_q });
}

void Vpn::waitForPort(int attempt) {
	const auto core = _core.data();
	if (!core || _stopping) {
		return;
	}
	const auto socket = new QTcpSocket();
	const auto port = _port;
	const auto finish = [=](bool ok) {
		socket->disconnect();
		socket->abort();
		socket->deleteLater();
		if (_core.data() != core || _port != port) {
			return;
		} else if (ok) {
			useProxy(true);
			setState(VpnState::On);
			base::call_delayed(60 * 1000, core, [=] { _restarts = 0; });
		} else if (attempt + 1 < kPortAttempts) {
			base::call_delayed(150, core, [=] { waitForPort(attempt + 1); });
		} else {
			setState(VpnState::Error, Tr(
				"The VPN core did not start.",
				"Ядро VPN не запустилось."));
		}
	};
	QObject::connect(socket, &QTcpSocket::connected, [=] { finish(true); });
	QObject::connect(socket, &QTcpSocket::errorOccurred, [=] {
		finish(false);
	});
	socket->connectToHost(QHostAddress::LocalHost, quint16(port));
}

void Vpn::coreFinished() {
	_core = nullptr;
	if (_stopping || !_enabled) {
		return;
	}
	if (++_restarts <= kMaxRestarts) {
		setState(VpnState::Starting);
		_restartTimer.callOnce(crl::time(1000) * _restarts * _restarts);
	} else {
		useProxy(false);
		setState(VpnState::Error, Tr(
			"The VPN core keeps stopping. Turn it off and on again.",
			"Ядро VPN останавливается. Выключите и включите его снова."));
	}
}

void Vpn::useProxy(bool use) {
	auto &app = Core::App();
	const auto &proxy = app.settings().proxy();
	if (use) {
		if (!IsOurProxy(proxy.selected()) && proxy.isEnabled()) {
			// Someone's own proxy: bring it back when we turn off.
			const auto was = proxy.selected();
			KeychainWrite(u"previous_proxy"_q, QJsonDocument(QJsonObject{
				{ u"type"_q, int(was.type) },
				{ u"host"_q, was.host },
				{ u"port"_q, int(was.port) },
				{ u"user"_q, was.user },
				{ u"password"_q, was.password },
			}).toJson(QJsonDocument::Compact));
		}
		auto data = MTP::ProxyData();
		data.type = MTP::ProxyData::Type::Socks5;
		data.host = u"127.0.0.1"_q;
		data.port = uint32(_port);
		data.user = _user;
		data.password = _password;
		app.setCurrentProxy(data, MTP::ProxyData::Settings::Enabled);
		return;
	} else if (!IsOurProxy(proxy.selected())) {
		return;
	}
	auto previous = MTP::ProxyData();
	if (const auto saved = KeychainRead(u"previous_proxy"_q)) {
		const auto object = QJsonDocument::fromJson(*saved).object();
		previous.type = MTP::ProxyData::Type(object.value(u"type"_q).toInt());
		previous.host = object.value(u"host"_q).toString();
		previous.port = uint32(object.value(u"port"_q).toInt());
		previous.user = object.value(u"user"_q).toString();
		previous.password = object.value(u"password"_q).toString();
		KeychainRemove(u"previous_proxy"_q);
	}
	app.setCurrentProxy(
		previous,
		previous.valid()
			? MTP::ProxyData::Settings::Enabled
			: MTP::ProxyData::Settings::System);
}

void Vpn::setState(VpnState state, QString error) {
	_error = error;
	_state.force_assign(state);
}

void Vpn::saveSecrets() const {
	KeychainWrite(u"link"_q, _link.toUtf8());
	KeychainWrite(u"body"_q, _body);
}

void Vpn::loadSecrets() {
	const auto link = KeychainRead(u"link"_q);
	if (!link) {
		return;
	}
	const auto token = TokenFromLink(QString::fromUtf8(*link));
	if (!token) {
		return;
	}
	_link = QString::fromUtf8(*link);
	_token = *token;
	if (const auto body = KeychainRead(u"body"_q)) {
		applyBody(*body);
	}
}

void Vpn::saveFlags() const {
	auto file = QFile(FlagsPath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"enabled"_q, _enabled },
			{ u"expires"_q, qint64(_expiresAt) },
		}).toJson(QJsonDocument::Compact));
	}
}

void Vpn::loadFlags() {
	auto file = QFile(FlagsPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	_enabled = object.value(u"enabled"_q).toBool();
	_expiresAt = TimeId(object.value(u"expires"_q).toInteger());
}

void VpnBox(not_null<Ui::GenericBox*> box) {
	auto &vpn = Vpn::Instance();
	box->setTitle(rpl::single(u"SHILLVPN"_q));
	box->setWidth(st::boxWideWidth);

	const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		vpn.stateValue() | rpl::map([=](VpnState) {
			return Vpn::Instance().statusText();
		}),
		st::boxLabel));
	status->setSelectable(false);
	box->addSkip(st::boxLittleSkip);

	const auto addLink = [&](rpl::producer<QString> text, Fn<void()> click) {
		const auto button = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				std::move(text),
				st::settingsButtonNoIcon),
			style::margins());
		button->setClickedCallback(std::move(click));
		return button;
	};

	if (!vpn.hasSubscription()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				"Telegram in ShillGramm works on any network through "
				"SHILLVPN. Paste your subscription link from @SHILLVPN_bot "
				"or from the cabinet on shillvpn.site.",
				"Telegram в ShillGramm работает в любой сети через "
				"SHILLVPN. Вставьте ссылку подписки из @SHILLVPN_bot "
				"или из личного кабинета на shillvpn.site.")),
			st::boxLabel));
		box->addSkip(st::boxLittleSkip);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::NoNewlines,
			rpl::single(Tr("Subscription link", "Ссылка подписки"))));
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addSkip(st::boxLittleSkip);
		addLink(
			rpl::single(Tr(
				"Buy or renew on shillvpn.site",
				"Купить или продлить на shillvpn.site")),
			[] { File::OpenUrl(QString::fromLatin1(kSite) + u"/app/buy/"_q); });

		const auto busy = box->lifetime().make_state<bool>(false);
		const auto submit = [=] {
			if (*busy) {
				return;
			}
			const auto link = field->getLastText().trimmed();
			if (!Vpn::TokenFromLink(link)) {
				field->showError();
				return;
			}
			*busy = true;
			const auto weak = base::make_weak(box);
			Vpn::Instance().setLink(link, [=](QString error) {
				if (const auto strong = weak.get()) {
					*busy = false;
					if (error.isEmpty()) {
						strong->closeBox();
					} else {
						strong->showToast(error);
						field->showError();
					}
				}
			});
		};
		field->submits() | rpl::on_next([=](auto) {
			submit();
		}, field->lifetime());
		box->addButton(rpl::single(Tr("Connect", "Подключить")), submit);
		box->addButton(rpl::single(Tr("Later", "Позже")), [=] {
			box->closeBox();
		});
		return;
	}

	addLink(
		rpl::single(Tr("Renew subscription", "Продлить подписку")),
		[=] {
			box->closeBox();
			OpenVpnRenew(nullptr);
		});
	addLink(
		rpl::single(Tr(
			"Connect a phone or another device",
			"Подключить телефон или другое устройство")),
		[] { File::OpenUrl(Vpn::Instance().connectPageUrl()); });
	addLink(
		rpl::single(Tr("Update the server list", "Обновить список серверов")),
		[] { Vpn::Instance().refresh(); });
	addLink(
		rpl::single(Tr("Use another link", "Сменить ссылку")),
		[=] {
			Vpn::Instance().forget();
			box->closeBox();
			ShowVpnBox();
		});

	vpn.stateValue() | rpl::on_next([=](VpnState state) {
		box->clearButtons();
		const auto on = Vpn::Instance().enabled();
		box->addButton(
			rpl::single(on
				? Tr("Turn off", "Выключить")
				: Tr("Turn on", "Включить")),
			[=] { Vpn::Instance().setEnabled(!on); });
		box->addButton(rpl::single(Tr("Close", "Закрыть")), [=] {
			box->closeBox();
		});
	}, box->lifetime());
}

void ShowVpnBox() {
	Ui::show(Box(VpnBox));
}

void ShowVpnBox(not_null<Window::SessionController*> controller) {
	controller->show(Box(VpnBox));
}

void OpenVpnRenew(Window::SessionController *controller) {
	if (!controller) {
		if (const auto window = Core::App().activePrimaryWindow()) {
			controller = window->sessionController();
		}
	}
	if (controller) {
		// The bot's Mini App right in the app: Stars, SBP, crypto.
		UrlClickHandler::Open(
			u"https://t.me/%1?startapp"_q.arg(QString::fromLatin1(kBot)),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
			}));
	} else {
		File::OpenUrl(QString::fromLatin1(kSite) + u"/app/buy/"_q);
	}
}

#ifndef Q_OS_MAC

namespace {

[[nodiscard]] QString SecretPath(const QString &key) {
	return cWorkingDir() + u"tdata/shillgramm_vpn_"_q + key;
}

} // namespace

bool KeychainWrite(const QString &key, const QByteArray &value) {
	auto file = QFile(SecretPath(key));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
	return file.write(value) == value.size();
}

std::optional<QByteArray> KeychainRead(const QString &key) {
	auto file = QFile(SecretPath(key));
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	return file.readAll();
}

void KeychainRemove(const QString &key) {
	QFile::remove(SecretPath(key));
}

#endif // !Q_OS_MAC

} // namespace Shill
