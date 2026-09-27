/*
ShillGramm: pinned chats without the Telegram limit.
*/
#include "shillgramm/shill_pins.h"

#include "data/data_folder.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Shill {
namespace {

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<LocalPins>> &Registry() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<LocalPins>>();
	return result;
}

} // namespace

LocalPins::LocalPins(not_null<Main::Session*> session)
: _session(session) {
	load();
}

not_null<LocalPins*> LocalPins::For(not_null<Main::Session*> session) {
	auto &registry = Registry();
	const auto i = registry.find(session);
	if (i != end(registry)) {
		return i->second.get();
	}
	const auto result = registry.emplace(
		session,
		std::make_unique<LocalPins>(session)).first->second.get();
	session->lifetime().add([=] {
		Registry().remove(session);
	});
	return result;
}

bool LocalPins::contains(not_null<const History*> history) const {
	return _peers.contains(history->peer->id);
}

int LocalPins::count(Data::Folder *folder) const {
	auto result = 0;
	for (const auto peerId : _peers) {
		const auto history = _session->data().historyLoaded(peerId);
		if (history
			&& history->folderKnown()
			&& history->folder() == folder
			&& history->isPinnedDialog(FilterId())) {
			++result;
		}
	}
	return result;
}

void LocalPins::add(not_null<History*> history) {
	if (_peers.emplace(history->peer->id).second) {
		save();
	}
}

void LocalPins::remove(not_null<History*> history) {
	if (_peers.remove(history->peer->id)) {
		save();
	}
}

void LocalPins::reapply(Data::Folder *folder) {
	const auto owner = &_session->data();
	for (const auto peerId : _peers) {
		const auto history = owner->historyLoaded(peerId);
		if (history
			&& history->folderKnown()
			&& history->folder() == folder
			&& !history->isPinnedDialog(FilterId())) {
			owner->setPinnedFromEntryList(history, true);
		}
	}
}

QString LocalPins::path() const {
	return cWorkingDir()
		+ u"tdata/shillgramm_pins_%1.json"_q.arg(_session->userId().bare);
}

void LocalPins::load() {
	auto file = QFile(path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto list = QJsonDocument::fromJson(file.readAll()).array();
	for (const auto &value : list) {
		const auto id = value.toString().toULongLong();
		if (id) {
			_peers.emplace(PeerId(PeerIdHelper(id)));
		}
	}
}

void LocalPins::save() const {
	auto list = QJsonArray();
	for (const auto peerId : _peers) {
		list.append(QString::number(peerId.value));
	}
	auto file = QFile(path());
	if (_peers.empty()) {
		file.remove();
	} else if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(list).toJson(QJsonDocument::Compact));
	}
}

} // namespace Shill
