/*
ShillGramm: Cmd+K command palette - chats, actions and settings in one place.
*/
#include "shillgramm/shill_palette.h"

#include "shillgramm/shill_panel.h"
#include "shillgramm/shill_snooze.h"
#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_quick_action.h"
#include "dialogs/dialogs_row.h"
#include "dialogs/ui/dialogs_quick_action.h"
#include "dialogs/ui/dialogs_quick_action_context.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "ui/widgets/elastic_scroll.h"
#include "mainwindow.h"
#include "ui/widgets/multi_select.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "window/main_window.h"
#include "window/themes/window_theme.h"
#include "window/window_adaptive.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtGui/QShortcut>
#include <QtWidgets/QTextEdit>

namespace Shill {
namespace {

constexpr auto kMaxRows = 9;
constexpr auto kMaxChats = 7;

struct Item {
	QString title;
	QString hint;
	const style::icon *icon = nullptr;
	PeerData *peer = nullptr;
	Ui::PeerUserpicView userpic;
	Fn<void()> action;
};

[[nodiscard]] bool Matches(const QString &query, const QString &text) {
	return query.isEmpty() || text.toLower().contains(query);
}

class PaletteContent final : public Ui::RpWidget {
public:
	PaletteContent(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close);

	void setInnerFocus();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void init();
	void rebuild(const QString &query);
	void addActions(const QString &query);
	void addChats(const QString &query);
	void addSnoozed(const QString &query);
	void addChat(not_null<History*> history, const QString &hint);
	void updateHeight();
	void select(int index);
	void activate(int index);
	void paintList(QPainter &p);
	[[nodiscard]] int rowAt(QPoint position) const;

	void layout();
	[[nodiscard]] static int RowHeight();

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _close;
	object_ptr<Ui::MultiSelect> _select;
	object_ptr<Ui::RpWidget> _list;
	std::vector<Item> _items;
	int _selected = 0;

};

int PaletteContent::RowHeight() {
	return style::ConvertScale(44);
}

PaletteContent::PaletteContent(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close)
: RpWidget(parent)
, _controller(controller)
, _close(std::move(close))
, _select(
	this,
	st::shillSpotlightSelect,
	rpl::single(Tr(
		"Chats, actions, settings...",
		"Чаты, действия, настройки...")))
, _list(this) {
	init();
}

void PaletteContent::init() {
	_select->setQueryChangedCallback([=](const QString &query) {
		rebuild(query);
	});
	_select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		activate(_selected);
	});
	_select->setCancelledCallback([=] {
		_close();
	});
	_select->setResizedCallback([=] {
		updateHeight();
	});

	_list->setMouseTracking(true);
	_list->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_list.data());
		paintList(p);
	}, _list->lifetime());
	_list->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto index = rowAt(
				static_cast<QMouseEvent*>(e.get())->pos());
			if (index >= 0) {
				select(index);
			}
		} else if (type == QEvent::MouseButtonRelease) {
			const auto event = static_cast<QMouseEvent*>(e.get());
			if (event->button() == Qt::LeftButton) {
				const auto index = rowAt(event->pos());
				if (index >= 0) {
					activate(index);
				}
			}
		}
	}, _list->lifetime());
	_controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		_list->update();
	}, lifetime());

	rebuild(QString());
}

void PaletteContent::setInnerFocus() {
	_select->setInnerFocus();
}

void PaletteContent::keyPressEvent(QKeyEvent *e) {
	const auto count = int(_items.size());
	if (e->key() == Qt::Key_Down && count) {
		select((_selected + 1) % count);
	} else if (e->key() == Qt::Key_Up && count) {
		select((_selected + count - 1) % count);
	} else if (e->key() == Qt::Key_Escape
		&& !_select->getQuery().isEmpty()) {
		_select->clearQuery();
	} else if (e->key() == Qt::Key_Escape) {
		_close();
	} else {
		RpWidget::keyPressEvent(e);
	}
}

void PaletteContent::resizeEvent(QResizeEvent *e) {
	layout();
}

void PaletteContent::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::shillSpotlightRadius;
	p.setPen(QPen(st::shadowFg, 1.));
	p.setBrush(st::boxBg);
	p.drawRoundedRect(
		QRectF(rect()).marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }),
		radius,
		radius);
}

void PaletteContent::layout() {
	const auto margin = st::shillSpotlightMargin;
	_select->resizeToWidth(width() - 2 * margin);
	_select->moveToLeft(margin, margin);
	_list->setGeometry(
		0,
		margin + _select->height() + margin / 2,
		width(),
		int(_items.size()) * RowHeight());
}

void PaletteContent::updateHeight() {
	const auto margin = st::shillSpotlightMargin;
	const auto listHeight = int(_items.size()) * RowHeight();
	resize(
		width(),
		margin
			+ _select->height()
			+ (listHeight ? (margin / 2 + listHeight) : 0)
			+ margin);
	layout();
}

void PaletteContent::rebuild(const QString &query) {
	const auto q = query.trimmed().toLower();
	_items.clear();
	if (q.isEmpty()) {
		addActions(q);
		addSnoozed(q);
		addChats(q);
	} else {
		addChats(q);
		addSnoozed(q);
		addActions(q);
	}
	if (_items.size() > kMaxRows) {
		_items.resize(kMaxRows);
	}
	_selected = 0;
	updateHeight();
	_list->update();
}

[[nodiscard]] QString NoteOf(not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	return user ? user->note().text.simplified() : QString();
}

void PaletteContent::addChat(not_null<History*> history, const QString &hint) {
	const auto peer = history->peer;
	const auto controller = _controller;
	const auto note = NoteOf(peer);
	_items.push_back(Item{
		.title = peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: peer->name(),
		.hint = note.isEmpty()
			? hint
			: (QString::fromUtf8("\xf0\x9f\x93\x9d ") + note),
		.peer = peer,
		.action = [=] { controller->showPeerHistory(peer); },
	});
}

void PaletteContent::addChats(const QString &query) {
	const auto session = &_controller->session();
	auto added = 0;
	auto seen = base::flat_set<not_null<PeerData*>>();
	const auto scan = [&](not_null<Dialogs::MainList*> list, bool archive) {
		for (const auto &row : list->indexed()->all()) {
			if (added >= (query.isEmpty() ? 4 : kMaxChats)) {
				return;
			}
			const auto history = row->key().history();
			if (!history || seen.contains(history->peer)) {
				continue;
			}
			const auto peer = history->peer;
			const auto username = peer->username();
			if (!query.isEmpty()
				&& !Matches(query, peer->name())
				&& !(peer->isSelf()
					&& Matches(query, tr::lng_saved_messages(tr::now)))
				&& (username.isEmpty() || !Matches(query, username))
				&& !(query.size() > 1 && Matches(query, NoteOf(peer)))) {
				continue;
			}
			seen.emplace(peer);
			addChat(history, archive
				? Tr("Archive", "Архив")
				: Tr("Chat", "Чат"));
			++added;
		}
	};
	scan(session->data().chatsList(), false);
	if (!query.isEmpty()) {
		if (const auto folder = session->data().folderLoaded(
				Data::Folder::kId)) {
			scan(folder->chatsList(), true);
		}
	}
}

void PaletteContent::addSnoozed(const QString &query) {
	const auto session = &_controller->session();
	const auto snooze = Snooze::For(session);
	for (const auto &[peerId, until] : snooze->list()) {
		const auto history = session->data().historyLoaded(peerId);
		if (!history) {
			continue;
		}
		const auto hint = Tr("Snoozed, back ", "Отложен, вернётся ")
			+ SnoozeWhenText(until);
		if (!query.isEmpty()
			&& !Matches(query, history->peer->name())
			&& !Matches(query, Tr("snoozed", "отложен"))) {
			continue;
		}
		addChat(history, hint);
	}
}

void PaletteContent::addActions(const QString &query) {
	const auto controller = _controller;
	const auto add = [&](
			QString title,
			const style::icon *icon,
			QString keywords,
			Fn<void()> action,
			QString hint = QString()) {
		if (!query.isEmpty()
			&& !Matches(query, title)
			&& !Matches(query, keywords)) {
			return;
		}
		_items.push_back(Item{
			.title = std::move(title),
			.hint = std::move(hint),
			.icon = icon,
			.action = std::move(action),
		});
	};

	// Actions with the chat that is open right now.
	if (const auto history = controller->activeChatCurrent().history()) {
		const auto peer = history->peer;
		const auto name = peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: peer->name();
		const auto quick = [=](Dialogs::Ui::QuickDialogAction action) {
			return [=] {
				Dialogs::PerformQuickDialogAction(
					controller,
					peer,
					action,
					FilterId());
			};
		};
		using Label = Dialogs::Ui::QuickDialogActionLabel;
		using Action = Dialogs::Ui::QuickDialogAction;
		if (Window::IsUnreadThread(history)) {
			add(
				tr::lng_context_mark_read(tr::now),
				&st::menuIconMarkRead,
				u"read прочитать"_q,
				quick(Action::Read),
				name);
		}
		const auto mute = Dialogs::ResolveQuickDialogLabel(
			history,
			Action::Mute,
			FilterId());
		if (mute == Label::Mute || mute == Label::Unmute) {
			const auto muted = (mute == Label::Unmute);
			add(
				muted
					? tr::lng_context_unmute(tr::now)
					: tr::lng_context_mute(tr::now),
				muted ? &st::menuIconUnmute : &st::menuIconMute,
				u"mute unmute sound звук уведомления"_q,
				quick(Action::Mute),
				name);
		}
		if (!history->fixedOnTopIndex()) {
			const auto pinned = history->isPinnedDialog(FilterId());
			add(
				pinned
					? tr::lng_context_unpin_from_top(tr::now)
					: tr::lng_context_pin_to_top(tr::now),
				pinned ? &st::menuIconUnpin : &st::menuIconPin,
				u"pin unpin закрепить открепить"_q,
				quick(Action::Pin),
				name);
		}
		if (CanSnooze(history)) {
			const auto snoozeFor = [=](TimeId seconds) {
				return [=] {
					Snooze::For(&history->session())->snooze(
						history,
						TimeId(QDateTime::currentSecsSinceEpoch())
							+ seconds);
				};
			};
			add(
				Tr("Snooze for 1 hour", "Отложить на 1 час"),
				&st::menuIconTimer,
				u"snooze later отложить потом"_q,
				snoozeFor(3600),
				name);
			const auto tomorrow = QDateTime(
				QDate::currentDate().addDays(
					(QTime::currentTime().hour() < 6) ? 0 : 1),
				QTime(9, 0));
			add(
				Tr("Snooze until morning, 9:00", "Отложить до утра, 9:00"),
				&st::menuIconTimer,
				u"snooze tomorrow morning отложить завтра утро"_q,
				[=] {
					Snooze::For(&history->session())->snooze(
						history,
						TimeId(tomorrow.toSecsSinceEpoch()));
				},
				name);
		}
	}

	// General actions.
	add(
		tr::lng_saved_messages(tr::now),
		&st::menuIconSavedMessages,
		u"saved favorites избранное заметки"_q,
		[=] { controller->showPeerHistory(controller->session().user()); });
	add(
		Tr("Archive", "Архив"),
		&st::menuIconArchive,
		u"archive архив"_q,
		[=] {
			controller->openFolder(
				controller->session().data().folder(Data::Folder::kId));
		});
	add(
		tr::lng_menu_contacts(tr::now),
		&st::menuIconUserShow,
		u"contacts people контакты люди"_q,
		[=] { controller->show(PrepareContactsBox(controller)); });
	add(
		tr::lng_menu_calls(tr::now),
		&st::menuIconPhone,
		u"calls phone звонки телефон"_q,
		[=] { ::Calls::ShowCallsBox(controller); });
	add(
		tr::lng_menu_settings(tr::now),
		&st::menuIconSettings,
		u"settings preferences настройки параметры"_q,
		[=] { controller->showSettings(); });
	add(
		tr::lng_create_group_title(tr::now),
		&st::menuIconGroups,
		u"new group create новая группа создать"_q,
		[=] { controller->showNewGroup(); });
	add(
		tr::lng_create_channel_title(tr::now),
		&st::menuIconChannel,
		u"new channel create новый канал создать"_q,
		[=] { controller->showNewChannel(); });
	if (!controller->adaptive().isOneColumn()) {
		const auto collapsed = ChatListCollapsed(controller);
		add(
			(collapsed
				? Tr("Expand chat list", "Развернуть список чатов")
				: Tr("Collapse chat list", "Свернуть список чатов")),
			collapsed ? &st::menuIconExpand : &st::menuIconCollapse,
			u"collapse expand narrow avatars свернуть развернуть узкий аватарки список"_q,
			[=] { ToggleChatListCollapsed(controller); });
		const auto preset = [&](QString title, QString keywords, int width) {
			add(
				Tr("Panel: ", "Панель: ") + title,
				&st::menuIconChats,
				u"panel view width sidebar панель вид ширина "_q + keywords,
				[=] { SetChatListWidth(controller, width); });
		};
		preset(
			Tr("compact", "компактная"),
			u"compact компактная"_q,
			style::ConvertScale(200));
		preset(
			Tr("standard", "обычная"),
			u"standard normal обычная"_q,
			style::ConvertScale(300));
		preset(
			Tr("wide", "широкая"),
			u"wide широкая"_q,
			style::ConvertScale(420));
		if (controller->session().data().chatsFilters().has()) {
			const auto horizontal
				= Core::App().settings().chatFiltersHorizontal();
			add(
				horizontal
					? Tr("Folder tabs on the left", "Папки слева")
					: Tr("Folder tabs at the top", "Папки сверху"),
				&st::menuIconShowInFolder,
				u"folders tabs top left папки вкладки сверху слева"_q,
				[=] {
					Core::App().settings().setChatFiltersHorizontal(
						!horizontal);
					Core::App().saveSettingsDelayed();
				});
		}
	}
	add(
		(Window::Theme::IsNightMode()
			? Tr("Day theme", "Дневная тема")
			: Tr("Night theme", "Ночная тема")),
		&st::menuIconNightMode,
		u"night day dark light theme ночь день тёмная темная светлая тема"_q,
		[=] {
			Window::Theme::ToggleNightModeWithConfirmation(
				&controller->window(),
				[] {
					Window::Theme::ToggleNightMode();
					Window::Theme::KeepApplied();
				});
		});
}

void PaletteContent::select(int index) {
	if (_selected != index) {
		_selected = index;
		_list->update();
	}
}

void PaletteContent::activate(int index) {
	if (index < 0 || index >= int(_items.size())) {
		return;
	}
	const auto action = _items[index].action;
	_close();
	if (action) {
		action();
	}
}

int PaletteContent::rowAt(QPoint position) const {
	const auto height = RowHeight();
	const auto index = position.y() / height;
	return (position.y() >= 0 && index < int(_items.size())) ? index : -1;
}

void PaletteContent::paintList(QPainter &p) {
	const auto height = RowHeight();
	const auto width = _list->width();
	const auto padding = st::boxRowPadding.left();
	const auto size = style::ConvertScale(30);
	auto hq = PainterHighQualityEnabler(p);
	for (auto i = 0; i != int(_items.size()); ++i) {
		auto &item = _items[i];
		const auto top = i * height;
		if (i == _selected) {
			const auto inset = padding / 2;
			const auto radius = st::boxRadius;
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(
				QRect(inset, top + 2, width - 2 * inset, height - 4),
				radius,
				radius);
		}
		const auto iconLeft = padding;
		const auto iconSize = size;
		const auto iconTop = top + (height - iconSize) / 2;
		if (item.peer) {
			item.peer->paintUserpicLeft(
				p,
				item.userpic,
				iconLeft,
				iconTop,
				width,
				iconSize);
		} else if (item.icon) {
			item.icon->paintInCenter(
				p,
				QRect(iconLeft, iconTop, iconSize, iconSize));
		}
		const auto textLeft = iconLeft + iconSize + padding * 2 / 3;
		auto hintWidth = 0;
		if (!item.hint.isEmpty()) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			const auto maxHint = (width - textLeft) / 2;
			const auto hint = st::normalFont->elided(item.hint, maxHint);
			hintWidth = st::normalFont->width(hint);
			p.drawText(
				width - padding - hintWidth,
				top + (height - st::normalFont->height) / 2
					+ st::normalFont->ascent,
				hint);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		const auto available = width - textLeft - padding - hintWidth
			- (hintWidth ? padding / 2 : 0);
		p.drawText(
			textLeft,
			top + (height - st::semiboldFont->height) / 2
				+ st::semiboldFont->ascent,
			st::semiboldFont->elided(item.title, available));
	}
}

// Spring with a light overshoot, like iOS Spotlight settling in place.
float64 Spring(float64 delta, float64 dt) {
	const auto end = 1. - std::exp(-6.) * std::cos(8.);
	return delta * (1. - std::exp(-6. * dt) * std::cos(8. * dt)) / end;
}

class SpotlightOverlay final : public Ui::RpWidget {
public:
	SpotlightOverlay(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller);

	void setPullProgress(float64 progress);
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] bool interactive() const;
	void open();
	void close();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void updatePanelGeometry();
	void animateTo(
		float64 to,
		crl::time duration,
		anim::transition transition,
		Fn<void()> done);

	const not_null<Window::SessionController*> _controller;
	object_ptr<PaletteContent> _panel;
	QImage _blurred;
	QPixmap _panelCache;
	Ui::Animations::Simple _animation;
	float64 _progress = 0.;
	bool _interactive = true;
	bool _closing = false;

};

QPointer<SpotlightOverlay> Opened;

SpotlightOverlay::SpotlightOverlay(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _panel(this, controller, [=] { close(); }) {
	// Blur a small copy of the window: cheap and very soft when scaled up.
	auto grab = Ui::GrabWidgetToImage(parent);
	const auto small = grab.scaled(
		std::max(grab.width() / 6, 1),
		std::max(grab.height() / 6, 1),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	_blurred = Images::BlurLargeImage(
		small.convertToFormat(QImage::Format_ARGB32_Premultiplied),
		4);

	_panel->hide();
	parent->sizeValue() | rpl::on_next([=](QSize size) {
		setGeometry(QRect(QPoint(), size));
		updatePanelGeometry();
	}, lifetime());
	_panel->heightValue() | rpl::on_next([=] {
		updatePanelGeometry();
	}, lifetime());
	show();
	raise();
}

void SpotlightOverlay::updatePanelGeometry() {
	const auto side = style::ConvertScale(24);
	const auto width = std::min(
		style::ConvertScale(560),
		std::max(this->width() - 2 * side, style::ConvertScale(240)));
	if (_panel->width() != width) {
		_panel->resize(width, _panel->height());
	}
	const auto top = std::max(int(height() * 0.14), side);
	_panel->move((this->width() - width) / 2, top);
	if (!_panel->isHidden()) {
		return;
	}
	_panelCache = QPixmap();
	update();
}

float64 SpotlightOverlay::progress() const {
	return _progress;
}

bool SpotlightOverlay::interactive() const {
	return _interactive;
}

void SpotlightOverlay::setPullProgress(float64 progress) {
	if (!_interactive) {
		return;
	}
	_progress = std::clamp(progress, 0., 1.);
	update();
}

void SpotlightOverlay::animateTo(
		float64 to,
		crl::time duration,
		anim::transition transition,
		Fn<void()> done) {
	_animation.stop();
	_animation.start([=](float64 value) {
		_progress = value;
		update();
		if (!_animation.animating() && done) {
			done();
		}
	}, _progress, to, duration, std::move(transition));
}

void SpotlightOverlay::open() {
	if (_closing) {
		return;
	}
	_interactive = false;
	setFocus();
	animateTo(1., 460, Spring, [=] {
		_panelCache = QPixmap();
		_panel->show();
		_panel->setInnerFocus();
		update();
	});
}

void SpotlightOverlay::close() {
	if (_closing) {
		return;
	}
	_closing = true;
	_interactive = false;
	if (!_panel->isHidden()) {
		_panelCache = Ui::GrabWidget(_panel.data());
		_panel->hide();
	}
	_controller->widget()->setInnerFocus();
	animateTo(0., 180, anim::easeOutCirc, [=] {
		deleteLater();
	});
}

void SpotlightOverlay::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const auto backdrop = std::clamp(_progress, 0., 1.);
	p.setOpacity(backdrop);
	p.drawImage(rect(), _blurred);
	p.fillRect(
		rect(),
		Window::Theme::IsNightMode()
			? QColor(0, 0, 0, 90)
			: QColor(255, 255, 255, 60));
	if (!_panel->isHidden()) {
		return;
	}
	if (_panelCache.isNull()) {
		_panelCache = Ui::GrabWidget(_panel.data());
	}
	const auto target = QRectF(_panel->geometry());
	const auto shift = (1. - _progress) * -style::ConvertScale(70);
	const auto scale = 0.92 + 0.08 * _progress;
	p.setOpacity(std::clamp(_progress * 1.5, 0., 1.));
	p.translate(target.center() + QPointF(0., shift));
	p.scale(scale, scale);
	p.translate(-target.center());
	p.drawPixmap(target.topLeft(), _panelCache);
}

void SpotlightOverlay::mousePressEvent(QMouseEvent *e) {
	if (!_interactive) {
		close();
	}
}

[[nodiscard]] SpotlightOverlay *CreateOverlay(
		not_null<Window::SessionController*> controller) {
	if (Opened) {
		return nullptr;
	}
	const auto parent = controller->widget()->bodyWidget();
	const auto result = Ui::CreateChild<SpotlightOverlay>(
		parent,
		controller);
	Opened = result;
	return result;
}

[[nodiscard]] bool ShowForActiveWindow() {
	if (Opened) {
		Opened->close();
		return true;
	}
	const auto window = Core::App().activeWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return false;
	}
	ShowCommandPalette(controller);
	return true;
}

} // namespace

void ShowCommandPalette(not_null<Window::SessionController*> controller) {
	if (const auto overlay = CreateOverlay(controller)) {
		overlay->open();
	}
}

void SetupPullToSearch(
		not_null<Ui::ElasticScroll*> scroll,
		not_null<Window::SessionController*> controller,
		Fn<bool()> allowed) {
	struct State {
		QPointer<SpotlightOverlay> overlay;
	};
	const auto state = scroll->lifetime().make_state<State>();
	rpl::combine(
		scroll->positionValue(),
		scroll->movementValue()
	) | rpl::on_next([=](
			Ui::ElasticScrollPosition position,
			Ui::ElasticScrollMovement movement) {
		const auto overlay = state->overlay.data();
		if (overlay && !overlay->interactive()) {
			return;
		}
		const auto pulled = std::max(-position.overscroll, 0);
		if (movement == Ui::ElasticScrollMovement::Progress) {
			if (!overlay) {
				if (!pulled || !allowed()) {
					return;
				}
				state->overlay = CreateOverlay(controller);
				if (!state->overlay) {
					return;
				}
			}
			state->overlay->setPullProgress(
				pulled / float64(st::shillSpotlightPull));
		} else if (overlay) {
			// Fingers lifted: finish like Spotlight, or slide back.
			if (overlay->progress() >= 0.5) {
				overlay->open();
			} else {
				overlay->close();
			}
			state->overlay = nullptr;
		}
	}, scroll->lifetime());
}

bool HandlePaletteShortcutEvent(
		not_null<QObject*> object,
		not_null<QShortcutEvent*> event) {
	if (event->key() != QKeySequence(u"ctrl+k"_q)) {
		return false;
	}
	if (const auto shortcut = qobject_cast<QShortcut*>(object.get())) {
		const auto edit = qobject_cast<QTextEdit*>(shortcut->parent());
		if (edit && edit->textCursor().hasSelection()) {
			return false; // Keep "insert link" for selected text.
		}
	}
	return ShowForActiveWindow();
}

bool HandlePaletteKeyPress(not_null<QKeyEvent*> event) {
	const auto modifiers = event->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	if (event->key() != Qt::Key_K || modifiers != Qt::ControlModifier) {
		return false;
	}
	return ShowForActiveWindow();
}

} // namespace Shill
