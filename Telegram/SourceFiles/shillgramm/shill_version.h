/*
ShillGramm: the version people see. Telegram's internal AppVersion stays as
is: local data (tdata) and updates are tied to it, lowering it would make
the app treat its own data as coming from a newer build.
*/
#pragma once

namespace Shill {

inline constexpr auto kVersionStr = "1.0";

// The name people see (owner, 27.09.2026). AppName and AppFile keep
// "ShillGramm": the data folder and the bundle are named after them, a new
// name there would log everyone out. Links stay @shillgramm.
inline constexpr auto kDisplayName = "SHILLGRAM";
inline constexpr auto kDisplayNameDesktop = "SHILLGRAM Desktop";

} // namespace Shill
