/*
ShillGramm: chat list panel - any width, view presets, folder tabs place.
*/
#pragma once

namespace Ui {
class PopupMenu;
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

[[nodiscard]] bool ChatListCollapsed(
	not_null<Window::SessionController*> controller);
void ToggleChatListCollapsed(
	not_null<Window::SessionController*> controller);

// 0 = avatars only.
void SetChatListWidth(
	not_null<Window::SessionController*> controller,
	int width);

void FillChatListViewMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller);

// Right click on the panel edge - view menu, double click - collapse.
void SetupChatListResizeArea(
	not_null<Ui::RpWidget*> area,
	not_null<Window::SessionController*> controller);

} // namespace Shill
