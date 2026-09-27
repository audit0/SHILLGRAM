/*
ShillGramm: the version people see. Telegram's internal AppVersion stays as
is: local data (tdata) and updates are tied to it, lowering it would make
the app treat its own data as coming from a newer build.
*/
#pragma once

namespace Shill {

inline constexpr auto kVersionStr = "1.0";

} // namespace Shill
