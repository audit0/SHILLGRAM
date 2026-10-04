/*
ShillGramm: NaCl crypto_box (X25519 + XSalsa20-Poly1305) on top of OpenSSL.
*/
#include "shillgramm/shill_naclbox.h"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace Shill::NaclBox {
namespace {

using Block = std::array<uint8_t, 64>;

// "expand 32-byte k"
constexpr uint32_t kSigma[4] = {
	0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
};

[[nodiscard]] uint32_t Load32(const uint8_t *p) {
	return uint32_t(p[0])
		| (uint32_t(p[1]) << 8)
		| (uint32_t(p[2]) << 16)
		| (uint32_t(p[3]) << 24);
}

void Store32(uint8_t *p, uint32_t value) {
	p[0] = uint8_t(value);
	p[1] = uint8_t(value >> 8);
	p[2] = uint8_t(value >> 16);
	p[3] = uint8_t(value >> 24);
}

[[nodiscard]] uint32_t Rotate(uint32_t value, int bits) {
	return (value << bits) | (value >> (32 - bits));
}

void DoubleRounds(uint32_t x[16]) {
	for (auto i = 0; i != 10; ++i) {
		// Columns.
		x[4] ^= Rotate(x[0] + x[12], 7);
		x[8] ^= Rotate(x[4] + x[0], 9);
		x[12] ^= Rotate(x[8] + x[4], 13);
		x[0] ^= Rotate(x[12] + x[8], 18);
		x[9] ^= Rotate(x[5] + x[1], 7);
		x[13] ^= Rotate(x[9] + x[5], 9);
		x[1] ^= Rotate(x[13] + x[9], 13);
		x[5] ^= Rotate(x[1] + x[13], 18);
		x[14] ^= Rotate(x[10] + x[6], 7);
		x[2] ^= Rotate(x[14] + x[10], 9);
		x[6] ^= Rotate(x[2] + x[14], 13);
		x[10] ^= Rotate(x[6] + x[2], 18);
		x[3] ^= Rotate(x[15] + x[11], 7);
		x[7] ^= Rotate(x[3] + x[15], 9);
		x[11] ^= Rotate(x[7] + x[3], 13);
		x[15] ^= Rotate(x[11] + x[7], 18);
		// Rows.
		x[1] ^= Rotate(x[0] + x[3], 7);
		x[2] ^= Rotate(x[1] + x[0], 9);
		x[3] ^= Rotate(x[2] + x[1], 13);
		x[0] ^= Rotate(x[3] + x[2], 18);
		x[6] ^= Rotate(x[5] + x[4], 7);
		x[7] ^= Rotate(x[6] + x[5], 9);
		x[4] ^= Rotate(x[7] + x[6], 13);
		x[5] ^= Rotate(x[4] + x[7], 18);
		x[11] ^= Rotate(x[10] + x[9], 7);
		x[8] ^= Rotate(x[11] + x[10], 9);
		x[9] ^= Rotate(x[8] + x[11], 13);
		x[10] ^= Rotate(x[9] + x[8], 18);
		x[12] ^= Rotate(x[15] + x[14], 7);
		x[13] ^= Rotate(x[12] + x[15], 9);
		x[14] ^= Rotate(x[13] + x[12], 13);
		x[15] ^= Rotate(x[14] + x[13], 18);
	}
}

void InitState(uint32_t x[16], const uint8_t key[32], const uint8_t in[16]) {
	x[0] = kSigma[0];
	x[5] = kSigma[1];
	x[10] = kSigma[2];
	x[15] = kSigma[3];
	for (auto i = 0; i != 4; ++i) {
		x[1 + i] = Load32(key + 4 * i);
		x[11 + i] = Load32(key + 16 + 4 * i);
		x[6 + i] = Load32(in + 4 * i);
	}
}

// HSalsa20: derives a subkey from a key and 16 bytes of input.
void HSalsa20(uint8_t out[32], const uint8_t key[32], const uint8_t in[16]) {
	uint32_t x[16];
	InitState(x, key, in);
	DoubleRounds(x);
	const int pick[8] = { 0, 5, 10, 15, 6, 7, 8, 9 };
	for (auto i = 0; i != 8; ++i) {
		Store32(out + 4 * i, x[pick[i]]);
	}
}

// One Salsa20 keystream block: 8-byte nonce, 64-bit block counter.
[[nodiscard]] Block Salsa20Block(
		const uint8_t key[32],
		const uint8_t nonce[8],
		uint64_t counter) {
	uint8_t in[16];
	std::memcpy(in, nonce, 8);
	for (auto i = 0; i != 8; ++i) {
		in[8 + i] = uint8_t(counter >> (8 * i));
	}
	uint32_t start[16];
	InitState(start, key, in);
	uint32_t x[16];
	std::memcpy(x, start, sizeof(x));
	DoubleRounds(x);
	auto result = Block();
	for (auto i = 0; i != 16; ++i) {
		Store32(result.data() + 4 * i, x[i] + start[i]);
	}
	return result;
}

// XSalsa20 keystream XORed into data, starting at keystream offset 32:
// the first 32 bytes of block zero are the Poly1305 key.
void XSalsa20Xor(
		uint8_t *data,
		size_t size,
		const uint8_t key[32],
		const uint8_t nonce[24],
		uint8_t polyKey[32]) {
	uint8_t subkey[32];
	HSalsa20(subkey, key, nonce);
	const auto tail = nonce + 16;
	auto counter = uint64_t(0);
	auto block = Salsa20Block(subkey, tail, counter++);
	std::memcpy(polyKey, block.data(), 32);
	auto used = size_t(32);
	for (size_t i = 0; i != size; ++i) {
		if (used == block.size()) {
			block = Salsa20Block(subkey, tail, counter++);
			used = 0;
		}
		data[i] ^= block[used++];
	}
	OPENSSL_cleanse(subkey, sizeof(subkey));
	OPENSSL_cleanse(block.data(), block.size());
}

[[nodiscard]] bool Poly1305(
		uint8_t out[16],
		const uint8_t key[32],
		const uint8_t *data,
		size_t size) {
	const auto mac = EVP_MAC_fetch(nullptr, "POLY1305", nullptr);
	if (!mac) {
		return false;
	}
	const auto context = EVP_MAC_CTX_new(mac);
	auto length = size_t(0);
	const auto ok = context
		&& EVP_MAC_init(context, key, 32, nullptr) == 1
		&& EVP_MAC_update(context, data, size) == 1
		&& EVP_MAC_final(context, out, &length, 16) == 1
		&& length == 16;
	EVP_MAC_CTX_free(context);
	EVP_MAC_free(mac);
	return ok;
}

// crypto_box_beforenm: HSalsa20 over the X25519 shared secret.
[[nodiscard]] bool SharedKey(
		uint8_t out[32],
		const QByteArray &theirPublicKey,
		const QByteArray &mySecretKey) {
	if (theirPublicKey.size() != kKeySize || mySecretKey.size() != kKeySize) {
		return false;
	}
	const auto mine = EVP_PKEY_new_raw_private_key(
		EVP_PKEY_X25519,
		nullptr,
		reinterpret_cast<const uint8_t*>(mySecretKey.constData()),
		kKeySize);
	const auto theirs = EVP_PKEY_new_raw_public_key(
		EVP_PKEY_X25519,
		nullptr,
		reinterpret_cast<const uint8_t*>(theirPublicKey.constData()),
		kKeySize);
	const auto context = mine ? EVP_PKEY_CTX_new(mine, nullptr) : nullptr;
	uint8_t shared[32];
	auto length = sizeof(shared);
	const auto ok = theirs
		&& context
		&& EVP_PKEY_derive_init(context) == 1
		&& EVP_PKEY_derive_set_peer(context, theirs) == 1
		&& EVP_PKEY_derive(context, shared, &length) == 1
		&& length == sizeof(shared);
	EVP_PKEY_CTX_free(context);
	EVP_PKEY_free(theirs);
	EVP_PKEY_free(mine);
	if (ok) {
		const uint8_t zero[16] = { 0 };
		HSalsa20(out, shared, zero);
	}
	OPENSSL_cleanse(shared, sizeof(shared));
	return ok;
}

} // namespace

KeyPair GenerateKeyPair() {
	const auto key = EVP_PKEY_Q_keygen(nullptr, nullptr, "X25519");
	if (!key) {
		return {};
	}
	auto result = KeyPair{
		.publicKey = QByteArray(kKeySize, 0),
		.secretKey = QByteArray(kKeySize, 0),
	};
	auto publicSize = size_t(kKeySize);
	auto secretSize = size_t(kKeySize);
	const auto ok = EVP_PKEY_get_raw_public_key(
		key,
		reinterpret_cast<uint8_t*>(result.publicKey.data()),
		&publicSize) == 1
		&& EVP_PKEY_get_raw_private_key(
			key,
			reinterpret_cast<uint8_t*>(result.secretKey.data()),
			&secretSize) == 1
		&& publicSize == kKeySize
		&& secretSize == kKeySize;
	EVP_PKEY_free(key);
	return ok ? result : KeyPair();
}

QByteArray RandomBytes(int size) {
	auto result = QByteArray(size, 0);
	if (RAND_bytes(reinterpret_cast<uint8_t*>(result.data()), size) != 1) {
		return QByteArray();
	}
	return result;
}

QByteArray Seal(
		const QByteArray &message,
		const QByteArray &nonce,
		const QByteArray &theirPublicKey,
		const QByteArray &mySecretKey) {
	uint8_t key[32];
	if (nonce.size() != kNonceSize
		|| !SharedKey(key, theirPublicKey, mySecretKey)) {
		return QByteArray();
	}
	auto result = QByteArray(kMacSize, 0) + message;
	const auto data = reinterpret_cast<uint8_t*>(result.data());
	uint8_t polyKey[32];
	XSalsa20Xor(
		data + kMacSize,
		size_t(message.size()),
		key,
		reinterpret_cast<const uint8_t*>(nonce.constData()),
		polyKey);
	const auto ok = Poly1305(
		data,
		polyKey,
		data + kMacSize,
		size_t(message.size()));
	OPENSSL_cleanse(key, sizeof(key));
	OPENSSL_cleanse(polyKey, sizeof(polyKey));
	return ok ? result : QByteArray();
}

std::optional<QByteArray> Open(
		const QByteArray &box,
		const QByteArray &nonce,
		const QByteArray &theirPublicKey,
		const QByteArray &mySecretKey) {
	uint8_t key[32];
	if (box.size() < kMacSize
		|| nonce.size() != kNonceSize
		|| !SharedKey(key, theirPublicKey, mySecretKey)) {
		return std::nullopt;
	}
	auto result = box.mid(kMacSize);
	const auto data = reinterpret_cast<uint8_t*>(result.data());
	const auto size = size_t(result.size());
	const auto bytes = reinterpret_cast<const uint8_t*>(nonce.constData());

	// Only the Poly1305 key first: the text is decrypted once it verifies.
	uint8_t polyKey[32];
	XSalsa20Xor(nullptr, 0, key, bytes, polyKey);
	uint8_t mac[16];
	const auto verified = Poly1305(
		mac,
		polyKey,
		reinterpret_cast<const uint8_t*>(box.constData()) + kMacSize,
		size)
		&& CRYPTO_memcmp(mac, box.constData(), kMacSize) == 0;
	if (verified) {
		XSalsa20Xor(data, size, key, bytes, polyKey);
	}
	OPENSSL_cleanse(key, sizeof(key));
	OPENSSL_cleanse(polyKey, sizeof(polyKey));
	if (!verified) {
		return std::nullopt;
	}
	return result;
}

QByteArray IncrementNonce(QByteArray nonce) {
	auto carry = 1;
	for (auto i = 0; i != nonce.size() && carry; ++i) {
		const auto value = uint8_t(nonce[i]) + carry;
		nonce[i] = char(uint8_t(value));
		carry = value >> 8;
	}
	return nonce;
}

} // namespace Shill::NaclBox
