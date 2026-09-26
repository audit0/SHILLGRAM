/*
ShillGramm: snooze a chat.

The chat goes to the archive right away and comes back at the chosen time
as unread, with a toast and a dock bounce. Kept locally per account.
*/
#pragma once

#include "base/timer.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

[[nodiscard]] QString Tr(const char *en, const char *ru);

class Snooze final {
public:
	explicit Snooze(not_null<Main::Session*> session);

	[[nodiscard]] static not_null<Snooze*> For(
		not_null<Main::Session*> session);

	[[nodiscard]] TimeId until(not_null<History*> history) const;
	void snooze(not_null<History*> history, TimeId until);
	void cancel(not_null<History*> history);

private:
	struct Entry {
		TimeId until = 0;
		bool wasArchived = false;
	};

	[[nodiscard]] QString path() const;
	void load();
	void save() const;
	void check();
	void wake(not_null<History*> history, const Entry &entry);

	const not_null<Main::Session*> _session;
	base::flat_map<PeerId, Entry> _entries;
	base::Timer _timer;

};

[[nodiscard]] bool CanSnooze(not_null<History*> history);

void FillSnoozeMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	not_null<History*> history);

[[nodiscard]] QString SnoozeMenuLabel(not_null<History*> history);

} // namespace Shill
