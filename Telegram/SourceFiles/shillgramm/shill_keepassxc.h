/*
ShillGramm: passkeys from KeePassXC.

KeePassXC keeps passkeys in its database but gives them only to browsers,
through its browser-integration socket; macOS and Windows do not see them,
so the native passkey sheet and the phone QR cannot use them. SHILLGRAM
talks to that socket the way KeePassXC-Browser does: an encrypted channel,
a one-time named connection the user approves in KeePassXC, then
passkeys-get / passkeys-register for telegram.org. KeePassXC shows its own
confirmation, signs with the key and returns the WebAuthn response; the
private key never leaves KeePassXC.

The connection id and its key are kept in tdata/shillgramm_keepassxc.json,
like the extension keeps them in the browser profile.
*/
#pragma once

#include "platform/platform_webauthn.h"

namespace Data::Passkey {
struct LoginData;
struct RegisterData;
} // namespace Data::Passkey

namespace Shill::KeePassXC {

// KeePassXC is running with browser integration, or is installed and can
// be started: worth offering in the passkey box.
[[nodiscard]] bool Available();

// Failures other than a cancel are explained with a toast.
void Login(
	const Data::Passkey::LoginData &data,
	Fn<void(Platform::WebAuthn::LoginResult)> done);
void Register(
	const Data::Passkey::RegisterData &data,
	Fn<void(Platform::WebAuthn::RegisterResult)> done);

} // namespace Shill::KeePassXC
