/*
ShillGramm: Cmd+K command palette - chats, actions and settings in one place.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

void ShowCommandPalette(not_null<Window::SessionController*> controller);

// Cmd+K from the app event filter. A field with selected text keeps
// its own Cmd+K ("insert link").
bool HandlePaletteShortcutEvent(
	not_null<QObject*> object,
	not_null<QShortcutEvent*> event);
bool HandlePaletteKeyPress(not_null<QKeyEvent*> event);

} // namespace Shill
