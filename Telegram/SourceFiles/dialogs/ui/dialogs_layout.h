/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/ui/dialogs_quick_action_context.h"
#include "ui/cached_round_corners.h"

namespace style {
struct DialogRow;
struct VerifiedBadge;
} // namespace style

namespace st {
extern const style::DialogRow &defaultDialogRow;
} // namespace st

namespace Data {
class Forum;
class Folder;
class Thread;
class CommunityInfo;
} // namespace Data

namespace Dialogs {
class Row;
class FakeRow;
class BasicRow;
struct RightButton;
} // namespace Dialogs

namespace Dialogs::Ui {

using namespace ::Ui;

class VideoUserpic;

struct TopicJumpCorners {
	CornersPixmaps normal;
	CornersPixmaps inverted;
	QPixmap small;
	int invertedRadius = 0;
	int smallKey = 0; // = `-radius` if top right else `radius`.
};

struct TopicJumpCache {
	TopicJumpCorners corners;
	TopicJumpCorners over;
	TopicJumpCorners selected;
	TopicJumpCorners rippleMask;
};

// ShillGramm: icon buttons (read / pin / mute) over the hovered chat row.
struct HoverActions {
	std::array<const style::icon*, 3> icons = {};
	int count = 0;
	int over = -1;
};

[[nodiscard]] QRect HoverActionRect(
	int width,
	const style::DialogRow &st,
	int count,
	int index);

struct PaintContext {
	RightButton *rightButton = nullptr;
	const HoverActions *hoverActions = nullptr;
	std::vector<QImage*> *chatsFilterTags = nullptr;
	QuickActionContext *quickActionContext = nullptr;
	not_null<const style::DialogRow*> st;
	TopicJumpCache *topicJumpCache = nullptr;
	Data::Folder *folder = nullptr;
	Data::Forum *forum = nullptr;
	Data::CommunityInfo *community = nullptr;
	required<QBrush> currentBg;
	FilterId filter = 0;
	float64 topicsExpanded = 0.;
	crl::time now = 0;
	QStringView searchLowerText;
	int width = 0;
	bool active = false;
	bool selected = false;
	float64 selectedOpacity = 1.; // ShillGramm: hover pill fades in
	bool topicJumpSelected = false;
	bool paused = false;
	bool search = false;
	bool narrow = false;
	bool displayUnreadInfo = false;
	bool insideCommunity = false;
};

extern const char kOptionDialogsMuteIcon[];

[[nodiscard]] const style::icon *ChatTypeIcon(
	not_null<PeerData*> peer,
	const PaintContext &context);
[[nodiscard]] const style::icon *ChatTypeIcon(not_null<PeerData*> peer);

[[nodiscard]] const style::VerifiedBadge &VerifiedStyle(
	const PaintContext &context);

class RowPainter {
public:
	static void Paint(
		Painter &p,
		not_null<const Row*> row,
		VideoUserpic *videoUserpic,
		const PaintContext &context);
	static void Paint(
		Painter &p,
		not_null<const FakeRow*> row,
		const PaintContext &context);
	static QRect SendActionAnimationRect(
		not_null<const Data::Thread*> thread,
		FilterId filterId,
		QRect rect,
		int fullWidth,
		bool textUpdated);
};

void PaintCollapsedRow(
	Painter &p,
	const BasicRow &row,
	Data::Folder *folder,
	const QString &text,
	int unread,
	const PaintContext &context);

int PaintRightButton(QPainter &p, const PaintContext &context);

} // namespace Dialogs::Ui
