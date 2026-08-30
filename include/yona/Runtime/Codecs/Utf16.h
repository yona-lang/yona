/*
 * UTF-8 ↔ LSP UTF-16 offset conversion (C ABI).
 *
 * Positions match the C++ mapper used by `yls` (`include/yona/Lsp/Utf16.h`):
 * 0-based line / UTF-16 code-unit columns, CRLF as one line break, non-BMP
 * scalar values as two UTF-16 units. A Yona-written language server can
 * call these symbols directly or via `Std\Utf16`.
 *
 * Strings are UTF-8. `nbytes` is the byte length; the NUL-terminated
 * wrappers use strlen. Offsets past the end are clamped.
 */

#ifndef YONA_RUNTIME_CODECS_UTF16_H
#define YONA_RUNTIME_CODECS_UTF16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void YonaRuntimeUtf8OffsetToUtf16(const char *Utf8, size_t ByteCount,
                                  size_t ByteOffset, int64_t *OutLine,
                                  int64_t *OutCharacter);

size_t YonaRuntimeUtf16PositionToUtf8(const char *Utf8, size_t ByteCount,
                                      int64_t Line, int64_t Character);

int64_t YonaRuntimeUtf8OffsetToUtf16Line(const char *Utf8, int64_t ByteOffset);
int64_t YonaRuntimeUtf8OffsetToUtf16Character(const char *Utf8,
                                              int64_t ByteOffset);
int64_t YonaRuntimeUtf16PositionToUtf8Offset(const char *Utf8, int64_t Line,
                                             int64_t Character);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_CODECS_UTF16_H */
