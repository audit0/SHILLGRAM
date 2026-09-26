/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
// The only translation unit that sees TDESKTOP_API_ID / TDESKTOP_API_HASH.
// There is deliberately no fallback to a shared test key.

#if !defined TDESKTOP_API_ID || !defined TDESKTOP_API_HASH
#error You are required to provide your own API_ID and API_HASH.
#endif

#define TDESKTOP_API_STRINGIFY2(x) #x
#define TDESKTOP_API_STRINGIFY(x) TDESKTOP_API_STRINGIFY2(x)

extern const int ApiId = TDESKTOP_API_ID;
extern const char ApiHash[] = TDESKTOP_API_STRINGIFY(TDESKTOP_API_HASH);
