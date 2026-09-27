/*
ShillGramm: glass for menus and windows.
*/
#include "shillgramm/shill_glass.h"

#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/layer_widget.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "window/themes/window_theme.h"
#include "styles/style_widgets.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

namespace Shill {
namespace {

constexpr auto kBackdropDuration = crl::time(220);
constexpr auto kLayerCheckDelay = crl::time(40);

class MenuGlassFilter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *object, QEvent *e) override {
		const auto type = e->type();
		if (type != QEvent::Show && type != QEvent::Resize) {
			return false;
		}
		const auto widget = qobject_cast<QWidget*>(object);
		if (!widget || !widget->isWindow()) {
			return false;
		}
		const auto menu = dynamic_cast<Ui::PopupMenu*>(widget);
		if (!menu) {
			return false;
		}
		const auto weak = QPointer<QWidget>(widget);
		// Native window is ready right after the show is processed.
		QTimer::singleShot(0, widget, [=] {
			if (!weak || !weak->isVisible()) {
				return;
			}
			ApplyNativePopupGlass(
				widget,
				menu->inner(),
				menu->st().radius,
				Window::Theme::IsNightMode());
		});
		return false;
	}

};

// Frosted copy of the window under boxes: opaque, prepared once, fades
// in and out with the layer. Nothing under it repaints meanwhile.
class LayerBackdrop final : public Ui::RpWidget {
public:
	LayerBackdrop(
		not_null<Ui::RpWidget*> body,
		not_null<Ui::LayerStackWidget*> layer)
	: RpWidget(body)
	, _layer(layer)
	, _check([=] { checkLayer(); }) {
		const auto wasVisible = !layer->isHidden();
		if (wasVisible) {
			layer->hide();
		}
		// Other opaque overlays (Spotlight) are not part of the window.
		auto hidden = std::vector<QPointer<QWidget>>();
		for (const auto child : body->findChildren<QWidget*>(
				QString(),
				Qt::FindDirectChildrenOnly)) {
			if (child != layer.get()
				&& child->isVisible()
				&& child->testAttribute(Qt::WA_OpaquePaintEvent)
				&& child->geometry() == body->rect()) {
				child->hide();
				hidden.push_back(child);
			}
		}
		auto snapshot = Ui::GrabWidgetToImage(body).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
		if (wasVisible) {
			layer->show();
		}
		for (const auto &child : hidden) {
			if (child) {
				child->show();
			}
		}
		const auto ratio = snapshot.devicePixelRatio();
		const auto small = snapshot.scaled(
			std::max(snapshot.width() / 14, 1),
			std::max(snapshot.height() / 14, 1),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		_blurred = Images::BlurLargeImage(
			small.convertToFormat(QImage::Format_ARGB32_Premultiplied),
			5
		).scaled(
			snapshot.size(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		_blurred.setDevicePixelRatio(ratio);
		_snapshot = std::move(snapshot);
		_size = body->size();

		setAttribute(Qt::WA_OpaquePaintEvent);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setGeometry(body->rect());
		body->sizeValue() | rpl::on_next([=](QSize size) {
			if (size != _size) {
				// Window resized: the copy is stale, just step aside.
				hide();
			}
			setGeometry(QRect(QPoint(), size));
		}, lifetime());
		stackUnder(layer);
		show();
		_shown = true;
		_animation.start([=] { update(); }, 0., 1., kBackdropDuration);
		_check.callEach(kLayerCheckDelay);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		const auto clip = e->rect();
		const auto ratio = _snapshot.devicePixelRatio();
		const auto source = QRect(
			int(clip.x() * ratio),
			int(clip.y() * ratio),
			int(clip.width() * ratio),
			int(clip.height() * ratio));
		// Crossfade: the window is translucent glass, layers must not add.
		const auto blur = _animation.value(_shown ? 1. : 0.);
		if (blur < 1.) {
			p.setOpacity(1. - blur);
			p.drawImage(clip, _snapshot, source);
		}
		if (blur > 0.) {
			p.setOpacity(blur);
			p.drawImage(clip, _blurred, source);
		}
	}

private:
	void checkLayer() {
		const auto shown = _layer && _layer->layerShown();
		if (_shown == shown) {
			return;
		}
		_shown = shown;
		_animation.start(
			[=] { update(); },
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			kBackdropDuration);
	}

	QPointer<Ui::LayerStackWidget> _layer;
	QImage _snapshot;
	QImage _blurred;
	QSize _size;
	Ui::Animations::Simple _animation;
	base::Timer _check;
	bool _shown = false;

};

} // namespace

void SetupMenuGlass() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	qApp->installEventFilter(new MenuGlassFilter(qApp));
}

void AttachLayerBackdrop(
		not_null<Ui::RpWidget*> body,
		not_null<Ui::LayerStackWidget*> layer) {
	const auto backdrop = Ui::CreateChild<LayerBackdrop>(body, layer);
	const auto weak = QPointer<QWidget>(backdrop);
	layer->lifetime().add([=] {
		if (const auto strong = weak.data()) {
			strong->deleteLater();
		}
	});
}

#ifndef Q_OS_MAC
void ApplyNativePopupGlass(
		not_null<QWidget*> window,
		QRect inner,
		int radius,
		bool dark) {
}
#endif // !Q_OS_MAC

} // namespace Shill
