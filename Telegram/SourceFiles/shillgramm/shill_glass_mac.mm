/*
ShillGramm: native glass behind popup menus on macOS.
*/
#include "shillgramm/shill_glass.h"

#include <AppKit/AppKit.h>

namespace Shill {
namespace {

NSString *const kGlassIdentifier = @"ShillGrammMenuGlass";

} // namespace

void ApplyNativePopupGlass(
		not_null<QWidget*> window,
		QRect inner,
		int radius,
		bool dark) {
	@autoreleasepool {
		NSView *view = reinterpret_cast<NSView*>(window->winId());
		NSWindow *nsWindow = [view window];
		if (!view || !nsWindow || inner.isEmpty()) {
			return;
		}
		[nsWindow setOpaque:NO];
		[nsWindow setBackgroundColor:[NSColor clearColor]];
		NSView *host = [view superview] ? [view superview] : view;
		const auto local = NSMakeRect(
			inner.x(),
			inner.y(),
			inner.width(),
			inner.height());
		const auto frame = (host == view)
			? local
			: [view convertRect:local toView:host];

		NSVisualEffectView *glass = nil;
		for (NSView *child in [host subviews]) {
			if ([[child identifier] isEqualToString:kGlassIdentifier]) {
				glass = (NSVisualEffectView*)child;
				break;
			}
		}
		if (!glass) {
			glass = [[NSVisualEffectView alloc] initWithFrame:frame];
			glass.identifier = kGlassIdentifier;
			glass.material = NSVisualEffectMaterialMenu;
			glass.blendingMode = NSVisualEffectBlendingModeBehindWindow;
			glass.state = NSVisualEffectStateActive;
			glass.wantsLayer = YES;
			glass.layer.masksToBounds = YES;
			if (host != view) {
				[host addSubview:glass
					positioned:NSWindowBelow
					relativeTo:view];
			} else {
				[host addSubview:glass positioned:NSWindowBelow relativeTo:nil];
			}
			[glass release];
		}
		glass.frame = frame;
		glass.layer.cornerRadius = radius;
		glass.appearance = [NSAppearance appearanceNamed:(dark
			? NSAppearanceNameDarkAqua
			: NSAppearanceNameAqua)];
	}
}

} // namespace Shill
