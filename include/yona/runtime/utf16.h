/*
 * UTF-8 ↔ LSP UTF-16 offset conversion (C ABI).
 *
 * Positions match the C++ mapper used by `yls` (`include/lsp/Utf16.h`):
 * 0-based line / UTF-16 code-unit columns, CRLF as one line break, non-BMP
 * scalar values as two UTF-16 units. A Yona-written language server can
 * call these symbols directly or via `Std\Utf16`.
 *
 * Strings are UTF-8. `nbytes` is the byte length; the NUL-terminated
 * wrappers use strlen. Offsets past the end are clamped.
 */

#ifndef YONA_RUNTIME_UTF16_H
#define YONA_RUNTIME_UTF16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void yona_utf8_offset_to_utf16(const char* utf8, size_t nbytes, size_t byte_offset,
                               int64_t* out_line, int64_t* out_character);

size_t yona_utf16_position_to_utf8(const char* utf8, size_t nbytes, int64_t line,
                                   int64_t character);

int64_t yona_utf8_offset_to_utf16_line(const char* utf8, int64_t byte_offset);
int64_t yona_utf8_offset_to_utf16_character(const char* utf8, int64_t byte_offset);
int64_t yona_utf16_position_to_utf8_offset(const char* utf8, int64_t line, int64_t character);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_UTF16_H */
