/*
ShillGramm: tells people a new SHILLGRAM is out.

The builds are not signed and Telegram's own updater is off, so nothing is
installed in the background. Twice a day the app asks GitHub for the latest
release of audit0/SHILLGRAM; when it is newer and carries a file for this
platform, a box offers the download. Nothing about the user is sent: the
request is a plain anonymous GET.
*/
#pragma once

#include "base/timer.h"

#include <QtCore/QPointer>

class QNetworkAccessManager;

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

class Updates final {
public:
	struct Release {
		QString version; // "1.1", without the "v".
		QString pageUrl; // The release page with the notes.
		QString fileUrl; // This platform's download.
	};

	[[nodiscard]] static Updates &Instance();

	// Application start: the first check comes a little later.
	void start();

	// A newer release with a file for this platform, once a check found it.
	[[nodiscard]] std::optional<Release> available() const;

private:
	Updates();

	void check();
	void request(bool viaProxy);
	void found(Release release);
	void loadState();
	void saveState() const;

	base::Timer _timer;
	QPointer<QNetworkAccessManager> _network;
	std::optional<Release> _available;
	QString _postponedVersion;
	TimeId _postponedUntil = 0;
	bool _checking = false;

};

void ShowUpdateBox(Window::SessionController *controller);

} // namespace Shill
