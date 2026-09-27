/*
ShillGramm: glass for menus and windows.

Menus are separate native windows on macOS: they get the system menu
material behind their rounded body. Boxes and layers live inside the main
window, so they get a frosted (blurred) copy of the window behind them.
*/
#pragma once

namespace Ui {
class RpWidget;
class LayerStackWidget;
} // namespace Ui

namespace Shill {

// Once per app: watches popup menus appearing.
void SetupMenuGlass();

// A frosted backdrop under the layer stack (boxes, main menu).
void AttachLayerBackdrop(
	not_null<Ui::RpWidget*> body,
	not_null<Ui::LayerStackWidget*> layer);

// Platform part: system material behind a popup window's rounded rect.
void ApplyNativePopupGlass(
	not_null<QWidget*> window,
	QRect inner,
	int radius,
	bool dark);

} // namespace Shill
