/*
ShillGramm: SHILLVPN built into the app.

The user's SHILLVPN subscription (the link from the bot or the site cabinet)
is fetched, its VLESS entries become an Xray config, and a bundled Xray core
serves a local SOCKS5 port with a random login. Telegram's own connection
goes through that port, so the app works before the Telegram login too.

The link and the fetched entries are secrets: they live in the macOS
Keychain (a 0600 file elsewhere), the config goes to Xray through stdin,
nothing of them is written to the log.
*/
#pragma once

#include "base/timer.h"

#include <QtCore/QJsonObject>
#include <QtCore/QPointer>

class QNetworkAccessManager;
class QProcess;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

enum class VpnState {
	None, // No subscription yet.
	Off,
	Loading, // Fetching the subscription.
	Starting, // The core is starting.
	On,
	Error,
};

class Vpn final {
public:
	[[nodiscard]] static Vpn &Instance();

	// Application start, after the settings are read: brings the core up
	// again if it was on, drops a stale local proxy otherwise.
	void start();
	// Application quit: the core must not outlive the app.
	void stop();

	[[nodiscard]] bool hasSubscription() const;
	[[nodiscard]] bool enabled() const;
	[[nodiscard]] VpnState state() const;
	[[nodiscard]] rpl::producer<VpnState> stateValue() const;
	[[nodiscard]] QString error() const;
	// Unix time the access ends, 0 when unknown.
	[[nodiscard]] TimeId expiresAt() const;
	// Short line for menus: "SHILLVPN · 3 дн.".
	[[nodiscard]] rpl::producer<QString> menuText() const;
	[[nodiscard]] QString statusText() const;

	// Accepts /sub/, /c/, /r/<app>/ links or the bare token; fetches it,
	// keeps it and turns the tunnel on. done("") on success.
	void setLink(const QString &link, Fn<void(QString error)> done);
	void setEnabled(bool enabled);
	void refresh();
	void forget();

	// First launch: three free days for this device from shillvpn.site
	// (one per device, the site decides). done("") once the tunnel runs
	// on the new subscription.
	void startTrial(Fn<void(QString error)> done);
	// The access came from the app's own trial: renewing goes to its
	// cabinet on the site, not to the Telegram account's Mini App.
	[[nodiscard]] bool hasCabinet() const;
	[[nodiscard]] QString cabinetUrl() const;

	// Pages on the site for this subscription: connect another device
	// (INCY, Happ...) and renew.
	[[nodiscard]] QString connectPageUrl() const;

	[[nodiscard]] static std::optional<QString> TokenFromLink(
		const QString &link);

private:
	struct Entry {
		QString title;
		QString uri;
	};

	Vpn();

	void fetch(Fn<void(QString error)> done);
	bool applyBody(const QByteArray &body);
	// POST to shillvpn.site/app/api; done(nullopt) when out of reach.
	void api(
		QJsonObject request,
		Fn<void(std::optional<QJsonObject> reply)> done);
	void waitTrialReady(int attempt, Fn<void(QString error)> done);
	void launch();
	void waitForPort(int attempt);
	void coreFinished();
	void useProxy(bool use);
	void setState(VpnState state, QString error = QString());
	void saveSecrets() const;
	void loadSecrets();
	void saveFlags() const;
	void loadFlags();
	[[nodiscard]] QByteArray buildConfig() const;
	[[nodiscard]] QString corePath() const;
	[[nodiscard]] QString subscriptionUrl() const;

	QString _link;
	QString _token;
	QByteArray _body; // Last fetched subscription body, as served.
	std::vector<Entry> _entries;
	TimeId _expiresAt = 0;
	bool _enabled = false;

	QString _cabinetKey; // Site cabinet of the app's own trial.
	bool _trialBusy = false;

	QPointer<QProcess> _core;
	QNetworkAccessManager *_network = nullptr;
	QNetworkAccessManager *_apiNetwork = nullptr;
	int _port = 0;
	QString _user;
	QString _password;
	int _restarts = 0;
	bool _stopping = false;
	base::Timer _restartTimer;
	base::Timer _refreshTimer;

	rpl::variable<VpnState> _state = VpnState::None;
	QString _error;

};

void VpnBox(not_null<Ui::GenericBox*> box);
void ShowVpnBox();
void ShowVpnBox(not_null<Window::SessionController*> controller);

// Renew: the SHILLVPN Mini App inside the app when logged in, the site
// cabinet otherwise.
void OpenVpnRenew(Window::SessionController *controller);

// Platform keychain; false when unavailable (then a 0600 file is used).
bool KeychainWrite(const QString &key, const QByteArray &value);
std::optional<QByteArray> KeychainRead(const QString &key);
void KeychainRemove(const QString &key);

} // namespace Shill
