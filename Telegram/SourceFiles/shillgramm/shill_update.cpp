/*
ShillGramm: tells people a new SHILLGRAM is out.
*/
#include "shillgramm/shill_update.h"

#include "base/unixtime.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "mtproto/mtproto_proxy_data.h"
#include "settings.h"
#include "shillgramm/shill_snooze.h" // Tr
#include "shillgramm/shill_version.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Shill {
namespace {

constexpr auto kFeed
	= "https://api.github.com/repos/audit0/SHILLGRAM/releases/latest";
constexpr auto kFirstCheck = 60 * 1000;
constexpr auto kCheckEvery = 12 * 3600 * crl::time(1000);
constexpr auto kFetchTimeout = 20 * 1000;
// "Later" or a download that was not installed: ask again in three days.
constexpr auto kPostpone = TimeId(3 * 86400);

// The release files are named SHILLGRAM-<version>-<platform>.<ext>.
[[nodiscard]] QString PlatformSuffix() {
#if defined Q_OS_MAC
	return u"-macOS-arm64.zip"_q;
#elif defined Q_OS_WIN
	return u"-Windows-x64.zip"_q;
#else
	return u"-Linux-x64.tar.xz"_q;
#endif
}

[[nodiscard]] QString HowToInstall() {
#if defined Q_OS_MAC
	return Tr(
		"Unpack the archive and replace SHILLGRAM.app in Applications. "
		"Your chats and settings stay.",
		"Распакуйте архив и замените SHILLGRAM.app в «Программах». "
		"Чаты и настройки сохранятся.");
#else
	return Tr(
		"Close SHILLGRAM, then unpack the archive over the old SHILLGRAM "
		"folder, replacing the files. Your chats and settings stay.",
		"Закройте SHILLGRAM и распакуйте архив поверх старой папки "
		"SHILLGRAM с заменой файлов. Чаты и настройки сохранятся.");
#endif
}

// For tests: SHILLGRAM_UPDATE_FEED=file:///path/latest.json.
[[nodiscard]] QUrl FeedUrl() {
	const auto custom = qEnvironmentVariable("SHILLGRAM_UPDATE_FEED");
	return QUrl(custom.isEmpty() ? QString::fromLatin1(kFeed) : custom);
}

// "v1.0.2" -> {1, 0, 2}; anything after '-' is ignored.
[[nodiscard]] std::vector<int> ParseVersion(QString text) {
	if (text.startsWith('v') || text.startsWith('V')) {
		text = text.mid(1);
	}
	text = text.section('-', 0, 0);
	auto result = std::vector<int>();
	for (const auto &part : text.split('.')) {
		auto ok = false;
		const auto number = part.toInt(&ok);
		if (!ok || number < 0) {
			return {};
		}
		result.push_back(number);
	}
	return result;
}

[[nodiscard]] bool IsNewer(const QString &theirs, const QString &ours) {
	auto a = ParseVersion(theirs);
	auto b = ParseVersion(ours);
	if (a.empty() || b.empty()) {
		return false;
	}
	const auto size = std::max(a.size(), b.size());
	a.resize(size, 0);
	b.resize(size, 0);
	return a > b;
}

[[nodiscard]] std::optional<Updates::Release> ParseRelease(
		const QJsonObject &object) {
	if (object.value(u"draft"_q).toBool()
		|| object.value(u"prerelease"_q).toBool()) {
		return std::nullopt;
	}
	auto version = object.value(u"tag_name"_q).toString();
	if (version.startsWith('v') || version.startsWith('V')) {
		version = version.mid(1);
	}
	if (!IsNewer(version, QString::fromLatin1(kVersionStr))) {
		return std::nullopt;
	}
	const auto suffix = PlatformSuffix();
	for (const auto &value : object.value(u"assets"_q).toArray()) {
		const auto asset = value.toObject();
		const auto name = asset.value(u"name"_q).toString();
		const auto url = asset.value(u"browser_download_url"_q).toString();
		if (name.startsWith(u"SHILLGRAM-"_q)
			&& name.endsWith(suffix)
			&& url.startsWith(u"https://"_q)) {
			return Updates::Release{
				.version = version,
				.pageUrl = object.value(u"html_url"_q).toString(),
				.fileUrl = url,
			};
		}
	}
	// A release for other platforms only: nothing to offer here.
	return std::nullopt;
}

[[nodiscard]] QString StatePath() {
	return cWorkingDir() + u"tdata/shillgramm_update.json"_q;
}

void UpdateBox(not_null<Ui::GenericBox*> box, Updates::Release release) {
	box->setTitle(rpl::single(u"SHILLGRAM "_q + release.version));
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(Tr(
			"A new version of SHILLGRAM is out: %1. You have %2.",
			"Вышла новая версия SHILLGRAM: %1. У вас %2.")
			.arg(release.version)
			.arg(QString::fromLatin1(kVersionStr))),
		st::boxLabel));
	box->addSkip(st::boxLittleSkip);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(HowToInstall()),
		st::boxDividerLabel));
	box->addButton(rpl::single(Tr("Download", "Скачать")), [=] {
		box->closeBox();
		File::OpenUrl(release.fileUrl);
	});
	box->addButton(rpl::single(Tr("Later", "Позже")), [=] {
		box->closeBox();
	});
	if (!release.pageUrl.isEmpty()) {
		box->addLeftButton(rpl::single(Tr("What's new", "Что нового")), [=] {
			File::OpenUrl(release.pageUrl);
		});
	}
}

} // namespace

Updates &Updates::Instance() {
	static auto instance = new Updates(); // Lives until exit.
	return *instance;
}

Updates::Updates()
: _timer([=] { check(); }) {
	loadState();
}

void Updates::start() {
	QTimer::singleShot(kFirstCheck, [] { Updates::Instance().check(); });
	_timer.callEach(kCheckEvery);
}

std::optional<Updates::Release> Updates::available() const {
	return _available;
}

void Updates::check() {
	if (_checking) {
		return;
	}
	_checking = true;
	request(false);
}

void Updates::request(bool viaProxy) {
	if (!_network) {
		_network = new QNetworkAccessManager();
	}
	// Directly first; where GitHub is out of reach, through the proxy
	// Telegram uses (SHILLVPN's local port when it is on).
	_network->setProxy(viaProxy
		? MTP::ToNetworkProxy(Core::App().settings().proxy().selected())
		: QNetworkProxy(QNetworkProxy::NoProxy));
	auto http = QNetworkRequest(FeedUrl());
	http.setRawHeader("Accept", "application/vnd.github+json");
	http.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"SHILLGRAM/"_q + QString::fromLatin1(kVersionStr));
	http.setTransferTimeout(kFetchTimeout);
	const auto reply = _network->get(http);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto document = QJsonDocument::fromJson(reply->readAll());
		if (reply->error() == QNetworkReply::NoError && document.isObject()) {
			_checking = false;
			auto release = ParseRelease(document.object());
			LOG(("SHILLGRAM update: latest %1, %2").arg(
				document.object().value(u"tag_name"_q).toString(),
				release ? u"offered"_q : u"nothing to offer"_q));
			if (release) {
				found(std::move(*release));
			}
			return;
		}
		LOG(("SHILLGRAM update: check failed%1, error %2").arg(
			viaProxy ? u" through the proxy"_q : QString(),
			QString::number(int(reply->error()))));
		const auto &proxy = Core::App().settings().proxy();
		const auto type = proxy.selected().type;
		if (!viaProxy
			&& proxy.isEnabled()
			&& (type == MTP::ProxyData::Type::Socks5
				|| type == MTP::ProxyData::Type::Http)) {
			request(true);
		} else {
			_checking = false; // Next time, in twelve hours.
		}
	});
}

void Updates::found(Release release) {
	_available = release;
	const auto now = base::unixtime::now();
	if (release.version == _postponedVersion && now < _postponedUntil) {
		return;
	}
	if (!Core::App().activePrimaryWindow()) {
		return; // The menu still offers it; the box comes with a next check.
	}
	_postponedVersion = release.version;
	_postponedUntil = now + kPostpone;
	saveState();
	ShowUpdateBox(nullptr);
}

void Updates::loadState() {
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	_postponedVersion = object.value(u"postponed"_q).toString();
	_postponedUntil = TimeId(object.value(u"until"_q).toInteger());
}

void Updates::saveState() const {
	auto file = QSaveFile(StatePath());
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(QJsonObject{
		{ u"postponed"_q, _postponedVersion },
		{ u"until"_q, qint64(_postponedUntil) },
	}).toJson(QJsonDocument::Compact));
	file.commit();
}

void ShowUpdateBox(Window::SessionController *controller) {
	const auto release = Updates::Instance().available();
	if (!release) {
		return;
	} else if (controller) {
		controller->show(Box(UpdateBox, *release));
	} else {
		// Over whatever is open, the SHILLVPN box included.
		Ui::show(Box(UpdateBox, *release), Ui::LayerOption::KeepOther);
	}
}

} // namespace Shill
