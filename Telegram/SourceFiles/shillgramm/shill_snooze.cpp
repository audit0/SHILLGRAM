/*
ShillGramm: snooze a chat.
*/
#include "shillgramm/shill_snooze.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/widgets/popup_menu.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtWidgets/QApplication>

namespace Shill {
namespace {

constexpr auto kMaxCheckDelay = crl::time(60 * 1000);
constexpr auto kRetryDelay = crl::time(15 * 1000);

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<Snooze>> &Registry() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Snooze>>();
	return result;
}

[[nodiscard]] QString FormatWhen(TimeId when) {
	const auto at = QDateTime::fromSecsSinceEpoch(when);
	const auto today = QDate::currentDate();
	const auto time = at.time().toString(u"HH:mm"_q);
	if (at.date() == today) {
		return Tr("today at ", "сегодня в ") + time;
	} else if (at.date() == today.addDays(1)) {
		return Tr("tomorrow at ", "завтра в ") + time;
	}
	return at.date().toString(u"dd.MM"_q) + Tr(" at ", " в ") + time;
}

[[nodiscard]] TimeId At(QDate date, int hour) {
	return TimeId(QDateTime(date, QTime(hour, 0)).toSecsSinceEpoch());
}

void ShowToast(not_null<Main::Session*> session, const QString &text) {
	if (const auto window = Core::App().activePrimaryWindow()) {
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == session) {
				controller->showToast(text);
			}
		}
	}
}

} // namespace

QString Tr(const char *en, const char *ru) {
	return Lang::GetInstance().id().startsWith(u"ru"_q)
		? QString::fromUtf8(ru)
		: QString::fromUtf8(en);
}

Snooze::Snooze(not_null<Main::Session*> session)
: _session(session)
, _timer([=] { check(); }) {
	load();
	check();
}

not_null<Snooze*> Snooze::For(not_null<Main::Session*> session) {
	auto &registry = Registry();
	const auto i = registry.find(session);
	if (i != end(registry)) {
		return i->second.get();
	}
	const auto result = registry.emplace(
		session,
		std::make_unique<Snooze>(session)).first->second.get();
	session->lifetime().add([=] {
		Registry().remove(session);
	});
	return result;
}

TimeId Snooze::until(not_null<History*> history) const {
	const auto i = _entries.find(history->peer->id);
	return (i != end(_entries)) ? i->second.until : TimeId();
}

void Snooze::snooze(not_null<History*> history, TimeId until) {
	const auto archived = Window::IsArchived(history);
	auto &entry = _entries[history->peer->id];
	if (!entry.until) {
		entry.wasArchived = archived;
	}
	entry.until = until;
	save();
	if (!archived) {
		_session->api().toggleHistoryArchived(history, true, nullptr);
	}
	ShowToast(
		_session,
		Tr("Snoozed until ", "Чат вернётся ") + FormatWhen(until));
	check();
}

void Snooze::cancel(not_null<History*> history) {
	const auto i = _entries.find(history->peer->id);
	if (i == end(_entries)) {
		return;
	}
	const auto entry = i->second;
	_entries.erase(i);
	save();
	if (!entry.wasArchived && Window::IsArchived(history)) {
		_session->api().toggleHistoryArchived(history, false, nullptr);
	}
	check();
}

std::vector<std::pair<PeerId, TimeId>> Snooze::list() const {
	auto result = std::vector<std::pair<PeerId, TimeId>>();
	for (const auto &[peerId, entry] : _entries) {
		result.emplace_back(peerId, entry.until);
	}
	ranges::sort(result, ranges::less(), &std::pair<PeerId, TimeId>::second);
	return result;
}

QString Snooze::path() const {
	return cWorkingDir()
		+ u"tdata/shillgramm_snooze_%1.json"_q.arg(
			_session->userId().bare);
}

void Snooze::load() {
	auto file = QFile(path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = object.begin(); i != object.end(); ++i) {
		const auto value = i.value().toObject();
		const auto id = i.key().toULongLong();
		const auto until = TimeId(value.value(u"until"_q).toInteger());
		if (id && until) {
			_entries[PeerId(PeerIdHelper(id))] = Entry{
				.until = until,
				.wasArchived = value.value(u"archived"_q).toBool(),
			};
		}
	}
}

void Snooze::save() const {
	auto object = QJsonObject();
	for (const auto &[peerId, entry] : _entries) {
		object.insert(QString::number(peerId.value), QJsonObject{
			{ u"until"_q, qint64(entry.until) },
			{ u"archived"_q, entry.wasArchived },
		});
	}
	auto file = QFile(path());
	if (_entries.empty()) {
		file.remove();
	} else if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}
}

void Snooze::check() {
	_timer.cancel();
	const auto now = base::unixtime::now();
	auto woken = std::vector<std::pair<not_null<History*>, Entry>>();
	auto waiting = false;
	auto next = TimeId();
	for (auto i = begin(_entries); i != end(_entries);) {
		const auto &entry = i->second;
		if (entry.until > now) {
			if (!next || entry.until < next) {
				next = entry.until;
			}
			++i;
			continue;
		}
		const auto history = _session->data().historyLoaded(i->first);
		if (!history || !history->folderKnown()) {
			waiting = true;
			++i;
			continue;
		}
		woken.emplace_back(history, entry);
		i = _entries.erase(i);
	}
	if (!woken.empty()) {
		save();
		for (const auto &[history, entry] : woken) {
			wake(history, entry);
		}
	}
	auto delay = kMaxCheckDelay;
	if (waiting) {
		delay = kRetryDelay;
	} else if (next) {
		delay = std::min(delay, crl::time(next - now) * 1000);
	} else if (_entries.empty()) {
		return;
	}
	_timer.callOnce(std::max(delay, crl::time(1000)));
}

void Snooze::wake(not_null<History*> history, const Entry &entry) {
	if (!entry.wasArchived && Window::IsArchived(history)) {
		_session->api().toggleHistoryArchived(history, false, nullptr);
	}
	if (!history->unreadMark() && !history->unreadCount()) {
		_session->data().histories().changeDialogUnreadMark(history, true);
	}
	ShowToast(
		_session,
		Tr("Snoozed chat is back: ", "Отложенный чат вернулся: ")
			+ history->peer->name());
	if (const auto window = Core::App().activePrimaryWindow()) {
		QApplication::alert(window->widget(), 0);
	}
}

QString SnoozeWhenText(TimeId when) {
	return FormatWhen(when);
}

bool CanSnooze(not_null<History*> history) {
	return Window::CanArchive(history, history->peer);
}

QString SnoozeMenuLabel(not_null<History*> history) {
	const auto until = Snooze::For(&history->session())->until(history);
	return until
		? (Tr("Snoozed: back ", "Отложен: вернётся ") + FormatWhen(until))
		: Tr("Snooze", "Отложить");
}

void FillSnoozeMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<History*> history) {
	const auto session = &history->session();
	const auto peerId = history->peer->id;
	const auto apply = [=](TimeId until) {
		return [=] {
			if (const auto history = session->data().historyLoaded(peerId)) {
				const auto snooze = Snooze::For(session);
				if (until) {
					snooze->snooze(history, until);
				} else {
					snooze->cancel(history);
				}
			}
		};
	};
	if (Snooze::For(session)->until(history)) {
		menu->addAction(
			Tr("Bring back now", "Вернуть сейчас"),
			apply(0),
			&st::menuIconUnarchive);
		menu->addSeparator();
	}
	const auto now = QDateTime::currentDateTime();
	const auto today = now.date();
	const auto base = TimeId(now.toSecsSinceEpoch());
	menu->addAction(
		Tr("For 1 hour", "На 1 час"),
		apply(base + 3600),
		&st::menuIconTimer);
	menu->addAction(
		Tr("For 3 hours", "На 3 часа"),
		apply(base + 3 * 3600),
		&st::menuIconTimer);
	if (now.time().hour() >= 6 && now.time().hour() < 17) {
		menu->addAction(
			Tr("Until this evening, 19:00", "До вечера, 19:00"),
			apply(At(today, 19)),
			&st::menuIconTimer);
	}
	if (now.time().hour() < 6) {
		menu->addAction(
			Tr("Until morning, 9:00", "До утра, 9:00"),
			apply(At(today, 9)),
			&st::menuIconTimer);
	} else {
		menu->addAction(
			Tr("Until tomorrow morning, 9:00", "До завтра, 9:00"),
			apply(At(today.addDays(1), 9)),
			&st::menuIconTimer);
	}
	menu->addAction(
		Tr("Until next week (Mon, 9:00)", "До понедельника, 9:00"),
		apply(At(today.addDays(8 - today.dayOfWeek()), 9)),
		&st::menuIconSchedule);
}

} // namespace Shill
