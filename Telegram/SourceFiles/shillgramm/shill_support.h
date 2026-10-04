/*
ShillGramm: «Report a problem» opens the support chat @SHILLSUP with a draft
that names the SHILLGRAM version, the system and the SHILLVPN state. The user
writes what happened and sends it: nothing is sent by itself, and the draft
has no account, phone, ID or subscription link in it.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Shill {

// "SHILLGRAM 1.1 · macOS 27.0 (arm64) · SHILLVPN: включён".
[[nodiscard]] QString ProblemReportHeader();

void ReportProblem(not_null<Window::SessionController*> controller);

} // namespace Shill
