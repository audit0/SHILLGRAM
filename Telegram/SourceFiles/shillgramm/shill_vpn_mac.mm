/*
ShillGramm: SHILLVPN secrets in the macOS Keychain.
*/
#include "shillgramm/shill_vpn.h"

#include <Foundation/Foundation.h>
#include <Security/Security.h>

namespace Shill {
namespace {

NSString *const kService = @"ShillGramm SHILLVPN";

[[nodiscard]] NSMutableDictionary *Query(const QString &key) {
	auto result = [NSMutableDictionary dictionary];
	result[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
	result[(__bridge id)kSecAttrService] = kService;
	result[(__bridge id)kSecAttrAccount] = key.toNSString();
	return result;
}

} // namespace

bool KeychainWrite(const QString &key, const QByteArray &value) {
	@autoreleasepool {
		const auto data = [NSData
			dataWithBytes:value.constData()
			length:NSUInteger(value.size())];
		auto query = Query(key);
		const auto update = @{ (__bridge id)kSecValueData: data };
		auto status = SecItemUpdate(
			(__bridge CFDictionaryRef)query,
			(__bridge CFDictionaryRef)update);
		if (status == errSecItemNotFound) {
			query[(__bridge id)kSecValueData] = data;
			query[(__bridge id)kSecAttrAccessible]
				= (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
			status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
		}
		return (status == errSecSuccess);
	}
}

std::optional<QByteArray> KeychainRead(const QString &key) {
	@autoreleasepool {
		auto query = Query(key);
		query[(__bridge id)kSecReturnData] = @YES;
		query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
		CFTypeRef result = nullptr;
		const auto status = SecItemCopyMatching(
			(__bridge CFDictionaryRef)query,
			&result);
		if (status != errSecSuccess || !result) {
			return std::nullopt;
		}
		// Not ARC here: the copied item is ours to release.
		const auto data = (__bridge NSData*)result;
		auto bytes = QByteArray(
			reinterpret_cast<const char*>(data.bytes),
			int(data.length));
		CFRelease(result);
		return bytes;
	}
}

void KeychainRemove(const QString &key) {
	@autoreleasepool {
		SecItemDelete((__bridge CFDictionaryRef)Query(key));
	}
}

} // namespace Shill
