/*
ShillGramm: Cmd+K command palette - chats, actions and settings in one place.
*/
#pragma once

namespace Ui {
class ElasticScroll;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

void ShowCommandPalette(not_null<Window::SessionController*> controller);

// Pull the chat list down past its top to reveal the palette, Spotlight
// style: it follows the fingers and settles in place when released.
void SetupPullToSearch(
	not_null<Ui::ElasticScroll*> scroll,
	not_null<Window::SessionController*> controller,
	Fn<bool()> allowed);

// The same gesture anywhere else in the window.
void SetupWindowPullToSearch(
	not_null<Window::SessionController*> controller);

// Cmd+K from the app event filter. A field with selected text keeps
// its own Cmd+K ("insert link").
bool HandlePaletteShortcutEvent(
	not_null<QObject*> object,
	not_null<QShortcutEvent*> event);
bool HandlePaletteKeyPress(not_null<QKeyEvent*> event);

} // namespace Shill
