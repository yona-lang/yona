#ifndef YONA_RUNTIME_PLATFORM_WINDOWSSOCKETS_H
#define YONA_RUNTIME_PLATFORM_WINDOWSSOCKETS_H

#if !defined(_WIN32)
#error "yona/Runtime/Platform/WindowsSockets.h is only available on Windows"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// The Windows SDK imposes this dependency order: Winsock must precede the
// general Windows header, while Mswsock requires both. Keep it explicit.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
// clang-format on

#endif /* YONA_RUNTIME_PLATFORM_WINDOWSSOCKETS_H */
