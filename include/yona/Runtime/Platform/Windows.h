#ifndef YONA_RUNTIME_PLATFORM_WINDOWS_H
#define YONA_RUNTIME_PLATFORM_WINDOWS_H

#if !defined(_WIN32)
#error "yona/Runtime/Platform/Windows.h is only available on Windows"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#endif /* YONA_RUNTIME_PLATFORM_WINDOWS_H */
