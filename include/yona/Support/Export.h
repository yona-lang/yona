#ifndef YONA_SUPPORT_EXPORT_H
#define YONA_SUPPORT_EXPORT_H

// Export/import macros for Windows DLL.
#ifdef YONA_STATIC_BUILD
// Building statically - no export/import needed.
#define YONA_API
#elif defined(_WIN32)
#ifdef YONA_LIB_EXPORTS
#define YONA_API __declspec(dllexport)
#else
#define YONA_API __declspec(dllimport)
#endif
#else
#define YONA_API
#endif

#endif /* YONA_SUPPORT_EXPORT_H */
