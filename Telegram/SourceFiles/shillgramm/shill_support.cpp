/*
ShillGramm: «Report a problem» with a ready draft for @SHILLSUP.
*/
#include "shillgramm/shill_support.h"

#include "base/platform/base_platform_info.h"
#include "shillgramm/shill_snooze.h" // Tr
#include "shillgramm/shill_version.h"
#include "shillgramm/shill_vpn.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QtCore/QSysInfo>

namespace Shill {
namespace {

constexpr auto kSupportUsername = "SHILLSUP";

[[nodiscard]] QString VpnText() {
	const auto &vpn = Vpn::Instance();
	if (!vpn.hasSubscription()) {
		return Tr("no subscription", "нет подписки");
	}
	switch (vpn.state()) {
	case VpnState::On: return Tr("on", "включён");
	case VpnState::Loading:
	case VpnState::Starting: return Tr("connecting", "подключается");
	case VpnState::Error: return Tr("error", "ошибка");
	case VpnState::None:
	case VpnState::Off: break;
	}
	return Tr("off", "выключен");
}

} // namespace

QString ProblemReportHeader() {
	return u"SHILLGRAM "_q
		+ QString::fromLatin1(kVersionStr)
		+ u" · "_q
		+ Platform::SystemVersionPretty()
		+ u" ("_q
		+ QSysInfo::currentCpuArchitecture()
		+ u") · SHILLVPN: "_q
		+ VpnText();
}

void ReportProblem(not_null<Window::SessionController*> controller) {
	const auto draft = ProblemReportHeader()
		+ u"\n\n"_q
		+ Tr("What happened: ", "Что случилось: ");
	controller->showPeerByLink(Window::PeerByLinkInfo{
		.usernameOrId = QString::fromLatin1(kSupportUsername),
		.text = draft,
	});
}

} // namespace Shill
