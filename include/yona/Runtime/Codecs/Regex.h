#ifndef YONA_RUNTIME_CODECS_REGEX_H
#define YONA_RUNTIME_CODECS_REGEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque, immutable compiled regular-expression handle. */
typedef struct YonaRegexOpaque *YonaRegexRef;

/**
 * Compile Pattern as a UTF/UCP PCRE2 expression.
 *
 * Returns an owned handle, or NULL when Pattern is NULL, invalid, or cannot
 * be compiled. The returned handle is immutable and may be used concurrently.
 * Release it with YonaRuntimeRegexRelease.
 */
YonaRegexRef YonaStdRegexCompile(const char *Pattern);

/**
 * Match Text against Regex.
 *
 * Regex and Text are borrowed for the call. Returns one on a match and zero
 * on no match or allocation failure. Concurrent calls are safe.
 */
int64_t YonaStdRegexMatches(YonaRegexRef Regex, const char *Text);

/**
 * Return the first match and capture groups as an owned runtime sequence.
 *
 * Regex and Text are borrowed. No match and failure both return an owned empty
 * sequence. Release the result with YonaRuntimeRegexRelease. Concurrent calls
 * are safe.
 */
int64_t *YonaStdRegexFind(YonaRegexRef Regex, const char *Text);

/**
 * Return all matches as an owned runtime sequence of owned match sequences.
 *
 * Regex and Text are borrowed. No match and failure both return an owned empty
 * sequence. Release the result with YonaRuntimeRegexRelease. Concurrent calls
 * are safe.
 */
int64_t *YonaStdRegexFindAll(YonaRegexRef Regex, const char *Text);

/**
 * Replace the first match and return an owned runtime string.
 *
 * All arguments are borrowed. On failure, an owned copy of Text is returned.
 * Release the result with YonaRuntimeRegexRelease. Concurrent calls are safe.
 */
char *YonaStdRegexReplace(YonaRegexRef Regex, const char *Text,
                          const char *Replacement);

/**
 * Replace every match and return an owned runtime string.
 *
 * All arguments are borrowed. On failure, an owned copy of Text is returned.
 * Release the result with YonaRuntimeRegexRelease. Concurrent calls are safe.
 */
char *YonaStdRegexReplaceAll(YonaRegexRef Regex, const char *Text,
                             const char *Replacement);

/**
 * Split Text and return an owned runtime sequence of owned strings.
 *
 * Regex and Text are borrowed. Failure returns an owned empty sequence.
 * Release the result with YonaRuntimeRegexRelease. Concurrent calls are safe.
 */
int64_t *YonaStdRegexSplit(YonaRegexRef Regex, const char *Text);

/** Retain a non-NULL runtime value returned by this API. Thread-safe. */
void YonaRuntimeRegexRetain(void *Value);

/** Release a non-NULL runtime value returned by this API. Thread-safe. */
void YonaRuntimeRegexRelease(void *Value);

/** Runtime finalizer hook for the compiled PCRE2 code stored in Regex. */
void YonaRuntimeRegexDisposeCompiledCode(void *CompiledCode);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_CODECS_REGEX_H */
