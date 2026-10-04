/*
ShillGramm: passkeys from KeePassXC.
*/
#include "shillgramm/shill_keepassxc.h"

#include "data/data_passkey_deserialize.h"
#include "settings.h"
#include "shillgramm/shill_naclbox.h"
#include "shillgramm/shill_snooze.h" // Tr
#include "webauthn/cable_box.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalSocket>

namespace Shill::KeePassXC {
namespace {

using Platform::WebAuthn::Error;
using Platform::WebAuthn::LoginResult;
using Platform::WebAuthn::RegisterResult;

constexpr auto kServerName = "org.keepassxc.KeePassXC.BrowserServer";
// The origin Telegram's own clientDataJSON carries, see
// data/data_passkey_deserialize.cpp; KeePassXC builds the same one.
constexpr auto kOrigin = "https://telegram.org";
constexpr auto kReplyTimeout = 10 * crl::time(1000);
// Unlocking the database or naming the new connection takes the user a while.
constexpr auto kUserTimeout = 5 * 60 * crl::time(1000);
constexpr auto kLaunchWait = 20 * crl::time(1000);
constexpr auto kLaunchRetry = crl::time(500);
constexpr auto kMaxAssociations = 8;

// src/browser/BrowserMessageBuilder.h in KeePassXC.
constexpr auto kDatabaseNotOpened = 1;
constexpr auto kActionDenied = 6;
constexpr auto kAssociationFailed = 8;
constexpr auto kIncorrectAction = 12;
constexpr auto kNoLogins = 15;
constexpr auto kCredentialExcluded = 21;
constexpr auto kRequestCanceled = 22;
// Ours: the reply did not decrypt or did not parse.
constexpr auto kBrokenReply = -1;

enum class Failure {
	None,
	NotRunning,
	Locked,
	Denied,
	NoPasskey,
	Excluded,
	Outdated,
	Timeout,
	Cancelled,
	Other,
};

struct Association {
	QString id;
	QString key;
};

[[nodiscard]] QString Base64(const QByteArray &bytes) {
	return QString::fromLatin1(bytes.toBase64());
}

[[nodiscard]] QByteArray FromBase64(const QJsonValue &value) {
	return QByteArray::fromBase64(value.toString().toLatin1());
}

[[nodiscard]] QString Base64Url(const QByteArray &bytes) {
	return QString::fromLatin1(bytes.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

[[nodiscard]] QByteArray FromBase64Url(const QJsonValue &value) {
	return QByteArray::fromBase64(
		value.toString().toLatin1(),
		QByteArray::Base64UrlEncoding);
}

[[nodiscard]] int ErrorCode(const QJsonValue &value) {
	// The outer replies carry it as a string, passkey errors as a number.
	const auto result = value.toVariant().toInt();
	return result ? result : kBrokenReply;
}

// Where KeePassXC listens, see BrowserShared::localServerPath().
[[nodiscard]] QStringList ServerCandidates() {
	const auto name = QString::fromLatin1(kServerName);
#if defined Q_OS_WIN
	return { name + '_' + qEnvironmentVariable("USERNAME") };
#elif defined Q_OS_MAC
	return {
		QStandardPaths::writableLocation(QStandardPaths::TempLocation)
			+ '/'
			+ name,
	};
#else
	const auto runtime = QStandardPaths::writableLocation(
		QStandardPaths::RuntimeLocation);
	return {
		runtime + u"/app/org.keepassxc.KeePassXC/"_q + name,
		runtime + '/' + name,
		QDir::homePath() + u"/snap/keepassxc/common/"_q + name,
	};
#endif
}

[[nodiscard]] bool ServerExists() {
#ifdef Q_OS_WIN
	return false; // A named pipe is only found by connecting to it.
#else
	for (const auto &path : ServerCandidates()) {
		if (QFile::exists(path)) {
			return true;
		}
	}
	return false;
#endif
}

[[nodiscard]] QString ServerPath() {
	const auto list = ServerCandidates();
#ifndef Q_OS_WIN
	for (const auto &path : list) {
		if (QFile::exists(path)) {
			return path;
		}
	}
#endif
	return list.front();
}

[[nodiscard]] QString InstalledApp() {
#if defined Q_OS_MAC
	for (const auto &path : {
		u"/Applications/KeePassXC.app"_q,
		QDir::homePath() + u"/Applications/KeePassXC.app"_q,
	}) {
		if (QDir(path).exists()) {
			return path;
		}
	}
	return QString();
#elif defined Q_OS_WIN
	const auto found = QStandardPaths::findExecutable(u"KeePassXC"_q);
	if (!found.isEmpty()) {
		return found;
	}
	for (const auto variable : { "ProgramFiles", "ProgramW6432" }) {
		const auto path = qEnvironmentVariable(variable)
			+ u"/KeePassXC/KeePassXC.exe"_q;
		if (QFile::exists(path)) {
			return path;
		}
	}
	return QString();
#else
	return QStandardPaths::findExecutable(u"keepassxc"_q);
#endif
}

[[nodiscard]] bool Launch() {
	const auto app = InstalledApp();
	if (app.isEmpty()) {
		return false;
	}
#ifdef Q_OS_MAC
	return QProcess::startDetached(u"/usr/bin/open"_q, { u"-a"_q, app });
#else
	return QProcess::startDetached(app, {});
#endif
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/shillgramm_keepassxc.json"_q;
}

[[nodiscard]] std::vector<Association> LoadAssociations() {
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto result = std::vector<Association>();
	const auto list = QJsonDocument::fromJson(
		file.readAll()
	).object().value(u"associations"_q).toArray();
	for (const auto &value : list) {
		const auto object = value.toObject();
		const auto id = object.value(u"id"_q).toString();
		const auto key = object.value(u"key"_q).toString();
		if (!id.isEmpty() && !key.isEmpty()) {
			result.push_back({ id, key });
		}
	}
	return result;
}

void SaveAssociations(const std::vector<Association> &list) {
	auto array = QJsonArray();
	for (const auto &association : list) {
		array.push_back(QJsonObject{
			{ u"id"_q, association.id },
			{ u"key"_q, association.key },
		});
	}
	const auto path = StoragePath();
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(QJsonObject{
		{ u"associations"_q, array },
	}).toJson(QJsonDocument::Compact));
	if (file.commit()) {
		QFile::setPermissions(
			path,
			QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	}
}

// One JSON object from the front of the stream, once it arrived whole.
// KeePassXC also broadcasts "database-locked" and such, so replies can
// come glued to notifications.
[[nodiscard]] std::optional<QByteArray> TakeObject(QByteArray &buffer) {
	const auto start = buffer.indexOf('{');
	if (start < 0) {
		buffer.clear();
		return std::nullopt;
	}
	auto depth = 0;
	auto inString = false;
	auto escaped = false;
	for (auto i = start; i != buffer.size(); ++i) {
		const auto ch = buffer[i];
		if (inString) {
			if (escaped) {
				escaped = false;
			} else if (ch == '\\') {
				escaped = true;
			} else if (ch == '"') {
				inString = false;
			}
		} else if (ch == '"') {
			inString = true;
		} else if (ch == '{') {
			++depth;
		} else if (ch == '}' && !--depth) {
			auto result = buffer.mid(start, i + 1 - start);
			buffer.remove(0, i + 1);
			return result;
		}
	}
	buffer.remove(0, start);
	return std::nullopt;
}

void ShowFailure(Failure failure, int code) {
	const auto text = [&] {
		switch (failure) {
		case Failure::NotRunning: return Tr(
			"KeePassXC is not answering. Open KeePassXC and turn on "
			"Settings → Browser Integration.",
			"KeePassXC не отвечает. Откройте KeePassXC и включите "
			"«Настройки → Интеграция с браузером».");
		case Failure::Locked: return Tr(
			"Unlock your KeePassXC database and try again.",
			"Разблокируйте базу KeePassXC и повторите.");
		case Failure::Denied: return Tr(
			"KeePassXC did not allow SHILLGRAM to connect.",
			"KeePassXC не разрешил подключение SHILLGRAM.");
		case Failure::NoPasskey: return Tr(
			"The open KeePassXC database has no passkey for telegram.org.",
			"В открытой базе KeePassXC нет ключа доступа "
			"для telegram.org.");
		case Failure::Excluded: return Tr(
			"This KeePassXC database already has a passkey "
			"for this account.",
			"В этой базе KeePassXC уже есть ключ доступа "
			"для этого аккаунта.");
		case Failure::Outdated: return Tr(
			"This KeePassXC has no passkey support. "
			"Update KeePassXC to 2.7.7 or newer.",
			"Эта версия KeePassXC не умеет ключи доступа. "
			"Обновите KeePassXC до 2.7.7 или новее.");
		case Failure::Timeout: return Tr(
			"KeePassXC did not answer in time.",
			"KeePassXC не ответил вовремя.");
		case Failure::None:
		case Failure::Cancelled:
			return QString();
		case Failure::Other:
			break;
		}
		return Tr(
			"KeePassXC could not use the passkey (error %1).",
			"KeePassXC не смог применить ключ доступа (ошибка %1)."
		).arg(code);
	}();
	if (!text.isEmpty()) {
		Platform::WebAuthn::Cable::ShowCableToast(text);
	}
}

class Operation final : public QObject {
public:
	using Done = Fn<void(QJsonObject credential, Failure failure, int code)>;

	Operation(bool isRegister, QJsonObject publicKey, int timeout, Done done);

	void start();

private:
	struct Reply {
		QJsonObject message;
		int errorCode = 0;
	};
	using Handler = Fn<void(Reply)>;
	struct Pending {
		QString action;
		QByteArray nonce;
		bool encrypted = false;
		Handler handler;
	};

	void connectToServer();
	void connectFailed();
	void handshake();
	void testAssociation(int index);
	void associate();
	void requestPasskey();
	void sendEncrypted(
		const QString &action,
		QJsonObject message,
		crl::time timeout,
		Handler handler);
	void write(
		QJsonObject outer,
		QByteArray nonce,
		bool encrypted,
		crl::time timeout,
		Handler handler);
	void received();
	void handle(const QJsonObject &outer);
	void fail(const Reply &reply);
	void finish(QJsonObject credential, Failure failure, int code = 0);

	const bool _isRegister = false;
	const QJsonObject _publicKey;
	const int _timeout = 0;
	Done _done;

	QLocalSocket *_socket = nullptr;
	QTimer *_timer = nullptr;
	QByteArray _buffer;
	NaclBox::KeyPair _keys;
	QByteArray _clientId;
	QByteArray _serverKey;
	std::vector<Association> _associations;
	std::optional<Pending> _pending;
	crl::time _launchDeadline = 0;
	bool _connected = false;
	bool _finished = false;

};

Operation::Operation(
	bool isRegister,
	QJsonObject publicKey,
	int timeout,
	Done done)
: _isRegister(isRegister)
, _publicKey(std::move(publicKey))
, _timeout(timeout)
, _done(std::move(done))
, _socket(new QLocalSocket(this))
, _timer(new QTimer(this)) {
	_timer->setSingleShot(true);
	connect(_timer, &QTimer::timeout, this, [=] {
		finish({}, Failure::Timeout);
	});
	connect(_socket, &QLocalSocket::connected, this, [=] {
		_connected = true;
		handshake();
	});
	connect(_socket, &QLocalSocket::errorOccurred, this, [=] {
		if (!_connected) {
			connectFailed();
		} else {
			finish({}, Failure::NotRunning);
		}
	});
	connect(_socket, &QLocalSocket::disconnected, this, [=] {
		if (_connected) {
			finish({}, Failure::NotRunning);
		}
	});
	connect(_socket, &QLocalSocket::readyRead, this, [=] {
		received();
	});
}

void Operation::start() {
	connectToServer();
}

void Operation::connectToServer() {
	if (_finished) {
		return;
	}
	_socket->abort();
	_socket->connectToServer(ServerPath());
}

void Operation::connectFailed() {
	if (_finished) {
		return;
	}
	// Not running: start KeePassXC once and wait for its socket.
	if (!_launchDeadline) {
		if (!Launch()) {
			finish({}, Failure::NotRunning);
			return;
		}
		_launchDeadline = crl::now() + kLaunchWait;
	} else if (crl::now() >= _launchDeadline) {
		finish({}, Failure::NotRunning);
		return;
	}
	QTimer::singleShot(kLaunchRetry, this, [=] { connectToServer(); });
}

void Operation::handshake() {
	_keys = NaclBox::GenerateKeyPair();
	_clientId = NaclBox::RandomBytes(NaclBox::kNonceSize);
	const auto nonce = NaclBox::RandomBytes(NaclBox::kNonceSize);
	if (_keys.publicKey.isEmpty() || _clientId.isEmpty() || nonce.isEmpty()) {
		finish({}, Failure::Other, kBrokenReply);
		return;
	}
	write(QJsonObject{
		{ u"action"_q, u"change-public-keys"_q },
		{ u"publicKey"_q, Base64(_keys.publicKey) },
		{ u"nonce"_q, Base64(nonce) },
		{ u"clientID"_q, Base64(_clientId) },
	}, nonce, false, kReplyTimeout, [=](Reply reply) {
		_serverKey = FromBase64(reply.message.value(u"publicKey"_q));
		if (reply.errorCode) {
			fail(reply);
		} else if (_serverKey.size() != NaclBox::kKeySize) {
			finish({}, Failure::Other, kBrokenReply);
		} else {
			_associations = LoadAssociations();
			testAssociation(0);
		}
	});
}

void Operation::testAssociation(int index) {
	if (index >= int(_associations.size())) {
		associate();
		return;
	}
	const auto &association = _associations[index];
	sendEncrypted(u"test-associate"_q, QJsonObject{
		{ u"id"_q, association.id },
		{ u"key"_q, association.key },
	}, kUserTimeout, [=](Reply reply) {
		if (reply.errorCode == kAssociationFailed) {
			// Made for another database, or removed in KeePassXC.
			testAssociation(index + 1);
		} else if (reply.errorCode) {
			fail(reply);
		} else {
			requestPasskey();
		}
	});
}

void Operation::associate() {
	const auto idKey = Base64(NaclBox::RandomBytes(NaclBox::kKeySize));
	sendEncrypted(u"associate"_q, QJsonObject{
		{ u"key"_q, Base64(_keys.publicKey) },
		{ u"idKey"_q, idKey },
	}, kUserTimeout, [=](Reply reply) {
		const auto id = reply.message.value(u"id"_q).toString();
		if (reply.errorCode) {
			fail(reply);
		} else if (id.isEmpty()) {
			finish({}, Failure::Other, kBrokenReply);
		} else {
			_associations.push_back({ id, idKey });
			if (_associations.size() > kMaxAssociations) {
				_associations.erase(begin(_associations));
			}
			SaveAssociations(_associations);
			requestPasskey();
		}
	});
}

void Operation::requestPasskey() {
	auto keys = QJsonArray();
	for (const auto &association : _associations) {
		keys.push_back(QJsonObject{
			{ u"id"_q, association.id },
			{ u"key"_q, association.key },
		});
	}
	const auto action = _isRegister
		? u"passkeys-register"_q
		: u"passkeys-get"_q;
	const auto timeout = std::max(crl::time(_timeout), kReplyTimeout)
		+ kUserTimeout;
	sendEncrypted(action, QJsonObject{
		{ u"publicKey"_q, _publicKey },
		{ u"origin"_q, QString::fromLatin1(kOrigin) },
		{ u"keys"_q, keys },
	}, timeout, [=](Reply reply) {
		const auto response = reply.message.value(u"response"_q).toObject();
		if (reply.errorCode) {
			fail(reply);
		} else if (response.contains(u"errorCode"_q)) {
			fail({ .errorCode = ErrorCode(response.value(u"errorCode"_q)) });
		} else if (response.isEmpty()) {
			finish({}, Failure::Other, kBrokenReply);
		} else {
			finish(response, Failure::None);
		}
	});
}

void Operation::sendEncrypted(
		const QString &action,
		QJsonObject message,
		crl::time timeout,
		Handler handler) {
	message.insert(u"action"_q, action);
	const auto nonce = NaclBox::RandomBytes(NaclBox::kNonceSize);
	const auto box = NaclBox::Seal(
		QJsonDocument(message).toJson(QJsonDocument::Compact),
		nonce,
		_serverKey,
		_keys.secretKey);
	if (nonce.isEmpty() || box.isEmpty()) {
		finish({}, Failure::Other, kBrokenReply);
		return;
	}
	write(QJsonObject{
		{ u"action"_q, action },
		{ u"message"_q, Base64(box) },
		{ u"nonce"_q, Base64(nonce) },
		{ u"clientID"_q, Base64(_clientId) },
		{ u"triggerUnlock"_q, u"true"_q },
	}, nonce, true, timeout, std::move(handler));
}

void Operation::write(
		QJsonObject outer,
		QByteArray nonce,
		bool encrypted,
		crl::time timeout,
		Handler handler) {
	_pending = Pending{
		.action = outer.value(u"action"_q).toString(),
		.nonce = std::move(nonce),
		.encrypted = encrypted,
		.handler = std::move(handler),
	};
	_timer->start(timeout);
	_socket->write(QJsonDocument(outer).toJson(QJsonDocument::Compact));
	_socket->flush();
}

void Operation::received() {
	_buffer.append(_socket->readAll());
	while (const auto object = TakeObject(_buffer)) {
		handle(QJsonDocument::fromJson(*object).object());
		if (_finished) {
			return;
		}
	}
}

void Operation::handle(const QJsonObject &outer) {
	if (!_pending
		|| outer.value(u"action"_q).toString() != _pending->action) {
		return;
	}
	_timer->stop();
	auto pending = std::move(*_pending);
	_pending = std::nullopt;

	auto reply = Reply();
	const auto nonce = FromBase64(outer.value(u"nonce"_q));
	if (outer.contains(u"errorCode"_q)) {
		reply.errorCode = ErrorCode(outer.value(u"errorCode"_q));
	} else if (nonce != NaclBox::IncrementNonce(pending.nonce)) {
		reply.errorCode = kBrokenReply;
	} else if (!pending.encrypted) {
		reply.message = outer;
	} else if (const auto opened = NaclBox::Open(
			FromBase64(outer.value(u"message"_q)),
			nonce,
			_serverKey,
			_keys.secretKey)) {
		reply.message = QJsonDocument::fromJson(*opened).object();
		if (reply.message.isEmpty()) {
			reply.errorCode = kBrokenReply;
		}
	} else {
		reply.errorCode = kBrokenReply;
	}
	pending.handler(reply);
}

void Operation::fail(const Reply &reply) {
	switch (reply.errorCode) {
	case kDatabaseNotOpened: finish({}, Failure::Locked); return;
	case kActionDenied: finish({}, Failure::Denied); return;
	case kIncorrectAction: finish({}, Failure::Outdated); return;
	case kNoLogins: finish({}, Failure::NoPasskey); return;
	case kCredentialExcluded: finish({}, Failure::Excluded); return;
	case kRequestCanceled: finish({}, Failure::Cancelled); return;
	}
	finish({}, Failure::Other, reply.errorCode);
}

void Operation::finish(QJsonObject credential, Failure failure, int code) {
	if (_finished) {
		return;
	}
	_finished = true;
	_timer->stop();
	_pending = std::nullopt;
	_socket->disconnect(this);
	_socket->abort();
	deleteLater();
	if (const auto done = base::take(_done)) {
		done(std::move(credential), failure, code);
	}
}

void Run(
		bool isRegister,
		QJsonObject publicKey,
		int timeout,
		Fn<void(QJsonObject, Failure)> done) {
	const auto operation = new Operation(
		isRegister,
		std::move(publicKey),
		timeout,
		[=](QJsonObject credential, Failure failure, int code) {
			ShowFailure(failure, code);
			done(std::move(credential), failure);
		});
	operation->start();
}

} // namespace

bool Available() {
	return ServerExists() || !InstalledApp().isEmpty();
}

void Login(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult)> done) {
	auto allow = QJsonArray();
	for (const auto &credential : data.allowCredentials) {
		allow.push_back(QJsonObject{
			{ u"type"_q, u"public-key"_q },
			{ u"id"_q, Base64Url(credential.id) },
		});
	}
	auto publicKey = QJsonObject{
		{ u"challenge"_q, Base64Url(data.challenge) },
		{ u"rpId"_q, data.rpId },
		{ u"timeout"_q, data.timeout },
		{ u"allowCredentials"_q, allow },
	};
	if (!data.userVerification.isEmpty()) {
		publicKey.insert(u"userVerification"_q, data.userVerification);
	}
	Run(false, publicKey, data.timeout, [=](
			QJsonObject credential,
			Failure failure) {
		auto result = LoginResult();
		const auto response = credential.value(u"response"_q).toObject();
		result.credentialId = FromBase64Url(credential.value(u"id"_q));
		result.authenticatorData = FromBase64Url(
			response.value(u"authenticatorData"_q));
		result.clientDataJSON = FromBase64Url(
			response.value(u"clientDataJSON"_q));
		result.signature = FromBase64Url(response.value(u"signature"_q));
		result.userHandle = FromBase64Url(response.value(u"userHandle"_q));
		if (failure != Failure::None
			|| result.credentialId.isEmpty()
			|| result.authenticatorData.isEmpty()
			|| result.clientDataJSON.isEmpty()
			|| result.signature.isEmpty()
			|| result.userHandle.isEmpty()) {
			if (failure == Failure::None) {
				ShowFailure(Failure::Other, kBrokenReply);
			}
			result = LoginResult();
			result.error = (failure == Failure::Cancelled)
				? Error::Cancelled
				: Error::Other;
		}
		done(result);
	});
}

void Register(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult)> done) {
	auto params = QJsonArray();
	for (const auto &param : data.pubKeyCredParams) {
		params.push_back(QJsonObject{
			{ u"type"_q, param.type },
			{ u"alg"_q, param.alg },
		});
	}
	const auto publicKey = QJsonObject{
		{ u"challenge"_q, Base64Url(data.challenge) },
		{ u"rp"_q, QJsonObject{
			{ u"id"_q, data.rp.id },
			{ u"name"_q, data.rp.name },
		} },
		{ u"user"_q, QJsonObject{
			{ u"id"_q, Base64Url(data.user.id) },
			{ u"name"_q, data.user.name },
			{ u"displayName"_q, data.user.displayName },
		} },
		{ u"pubKeyCredParams"_q, params },
		{ u"timeout"_q, data.timeout },
		{ u"attestation"_q, u"none"_q },
		{ u"authenticatorSelection"_q, QJsonObject{
			{ u"residentKey"_q, u"required"_q },
			{ u"requireResidentKey"_q, true },
			{ u"userVerification"_q, u"preferred"_q },
		} },
	};
	Run(true, publicKey, data.timeout, [=](
			QJsonObject credential,
			Failure failure) {
		auto result = RegisterResult();
		const auto response = credential.value(u"response"_q).toObject();
		result.credentialId = FromBase64Url(credential.value(u"id"_q));
		result.attestationObject = FromBase64Url(
			response.value(u"attestationObject"_q));
		result.clientDataJSON = FromBase64Url(
			response.value(u"clientDataJSON"_q));
		result.success = (failure == Failure::None)
			&& !result.credentialId.isEmpty()
			&& !result.attestationObject.isEmpty()
			&& !result.clientDataJSON.isEmpty();
		if (!result.success) {
			if (failure == Failure::None) {
				ShowFailure(Failure::Other, kBrokenReply);
			}
			result = RegisterResult();
			result.error = (failure == Failure::Cancelled)
				? Error::Cancelled
				: Error::Other;
		}
		done(result);
	});
}

} // namespace Shill::KeePassXC
