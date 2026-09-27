/*
ShillGramm: chat list panel - any width, view presets, folder tabs place.
*/
#include "shillgramm/shill_panel.h"

#include "shillgramm/shill_snooze.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/rp_widget.h"
#include "ui/widgets/popup_menu.h"
#include "mainwindow.h"
#include "window/main_window.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Shill {
namespace {

float64 SavedRatio = 0.;

[[nodiscard]] int BodyWidth(
		not_null<Window::SessionController*> controller) {
	return std::max(
		controller->widget()->bodyWidget()->width()
			- controller->filtersWidth(),
		1);
}

[[nodiscard]] int CurrentWidth(
		not_null<Window::SessionController*> controller) {
	const auto nochat = !controller->mainSectionShown();
	const auto ratio = Core::App().settings().dialogsWidthRatio(nochat);
	return ratio > 0. ? int(std::round(ratio * BodyWidth(controller))) : 0;
}

} // namespace

bool ChatListCollapsed(not_null<Window::SessionController*> controller) {
	return !CurrentWidth(controller);
}

void SetChatListWidth(
		not_null<Window::SessionController*> controller,
		int width) {
	auto &settings = Core::App().settings();
	const auto nochat = !controller->mainSectionShown();
	const auto body = BodyWidth(controller);
	const auto current = settings.dialogsWidthRatio(nochat);
	if (!width) {
		if (current > 0.) {
			SavedRatio = current;
		}
		settings.updateDialogsWidthRatio(0., nochat);
	} else {
		const auto minimal = controller->dialogsSmallColumnWidth() + 1;
		const auto maximal = std::max(
			body - int(st::columnMinimalWidthMain),
			minimal);
		settings.updateDialogsWidthRatio(
			std::clamp(width, minimal, maximal) / float64(body),
			nochat);
	}
	Core::App().saveSettingsDelayed();
	controller->updateColumnLayout();
}

void ToggleChatListCollapsed(
		not_null<Window::SessionController*> controller) {
	if (!ChatListCollapsed(controller)) {
		SetChatListWidth(controller, 0);
	} else {
		const auto body = BodyWidth(controller);
		SetChatListWidth(
			controller,
			(SavedRatio > 0.)
				? int(std::round(SavedRatio * body))
				: int(st::columnMinimalWidthLeft));
	}
}

void FillChatListViewMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller) {
	const auto current = CurrentWidth(controller);
	struct Preset {
		QString text;
		int width = 0;
	};
	const auto presets = std::vector<Preset>{
		{ Tr("Avatars only", "Только аватарки"), 0 },
		{ Tr("Compact", "Компактная"), style::ConvertScale(200) },
		{ Tr("Standard", "Обычная"), style::ConvertScale(300) },
		{ Tr("Wide", "Широкая"), style::ConvertScale(420) },
	};
	auto closest = 0;
	for (auto i = 1; i != int(presets.size()); ++i) {
		if (std::abs(presets[i].width - current)
			< std::abs(presets[closest].width - current)) {
			closest = i;
		}
	}
	const auto exact = [&](int i) {
		return (i == closest)
			&& (std::abs(presets[i].width - current)
				<= style::ConvertScale(12));
	};
	for (auto i = 0; i != int(presets.size()); ++i) {
		const auto width = presets[i].width;
		menu->addAction(
			presets[i].text,
			[=] { SetChatListWidth(controller, width); },
			exact(i) ? &st::mediaPlayerMenuCheck : nullptr);
	}
	if (controller->session().data().chatsFilters().has()) {
		menu->addSeparator();
		const auto horizontal = Core::App().settings().chatFiltersHorizontal();
		const auto setHorizontal = [=](bool value) {
			return [=] {
				Core::App().settings().setChatFiltersHorizontal(value);
				Core::App().saveSettingsDelayed();
			};
		};
		menu->addAction(
			Tr("Folder tabs on the left", "Папки слева"),
			setHorizontal(false),
			horizontal ? nullptr : &st::mediaPlayerMenuCheck);
		menu->addAction(
			Tr("Folder tabs at the top", "Папки сверху"),
			setHorizontal(true),
			horizontal ? &st::mediaPlayerMenuCheck : nullptr);
	}
}

void SetupChatListResizeArea(
		not_null<Ui::RpWidget*> area,
		not_null<Window::SessionController*> controller) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = area->lifetime().make_state<State>();
	area->setToolTip(Tr(
		"Drag to resize. Double-click to collapse, right-click for views",
		"Тяните, чтобы менять ширину. Двойной щелчок - свернуть, "
		"правый - виды панели"));
	area->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonDblClick) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				ToggleChatListCollapsed(controller);
			}
		} else if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::RightButton) {
				state->menu = base::make_unique_q<Ui::PopupMenu>(
					area,
					st::popupMenuWithIcons);
				FillChatListViewMenu(state->menu.get(), controller);
				state->menu->popup(QCursor::pos());
			}
		}
	}, area->lifetime());
}

} // namespace Shill
