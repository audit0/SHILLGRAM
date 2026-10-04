/*
ShillGramm: NaCl crypto_box (X25519 + XSalsa20-Poly1305) on top of OpenSSL.

KeePassXC's browser protocol encrypts every message with crypto_box_easy
from libsodium. The app has no libsodium, so the box is rebuilt here from
OpenSSL's X25519 and Poly1305 plus a small Salsa20 core.
*/
#pragma once

#include <QtCore/QByteArray>

#include <optional>

namespace Shill::NaclBox {

inline constexpr auto kKeySize = 32;
inline constexpr auto kNonceSize = 24;
inline constexpr auto kMacSize = 16;

struct KeyPair {
	QByteArray publicKey;
	QByteArray secretKey;
};

// Empty keys when OpenSSL fails.
[[nodiscard]] KeyPair GenerateKeyPair();
[[nodiscard]] QByteArray RandomBytes(int size);

// crypto_box_easy: mac || ciphertext. Empty on bad keys.
[[nodiscard]] QByteArray Seal(
	const QByteArray &message,
	const QByteArray &nonce,
	const QByteArray &theirPublicKey,
	const QByteArray &mySecretKey);

// crypto_box_open_easy. std::nullopt when the box does not verify.
[[nodiscard]] std::optional<QByteArray> Open(
	const QByteArray &box,
	const QByteArray &nonce,
	const QByteArray &theirPublicKey,
	const QByteArray &mySecretKey);

// sodium_increment: the nonce as a little-endian number plus one.
[[nodiscard]] QByteArray IncrementNonce(QByteArray nonce);

} // namespace Shill::NaclBox
