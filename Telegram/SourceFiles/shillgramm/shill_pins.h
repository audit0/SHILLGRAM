/*
ShillGramm: pinned chats without the Telegram limit.

Telegram's server pins at most 5 chats (10 with Premium, 100/200 in the
archive) and answers PINNED_DIALOGS_TOO_MUCH above that; a client cannot
lift it. Past the limit a chat is pinned here instead: in this app on this
device, kept locally per account, never sent to the server. Server pins
come first, local ones follow them in the pinned block.
*/
#pragma once

class History;

namespace Data {
class Folder;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Shill {

class LocalPins final {
public:
	// Room for local pins on top of the server's limit in a pinned list.
	static constexpr auto kMax = 100;

	explicit LocalPins(not_null<Main::Session*> session);

	[[nodiscard]] static not_null<LocalPins*> For(
		not_null<Main::Session*> session);

	[[nodiscard]] bool contains(not_null<const History*> history) const;
	// Local pins in this list (main list or a folder) that are shown now.
	[[nodiscard]] int count(Data::Folder *folder) const;

	void add(not_null<History*> history);
	void remove(not_null<History*> history);

	// After the server replaced a pinned list: pin ours again.
	void reapply(Data::Folder *folder);

private:
	[[nodiscard]] QString path() const;
	void load();
	void save() const;

	const not_null<Main::Session*> _session;
	base::flat_set<PeerId> _peers;

};

} // namespace Shill
