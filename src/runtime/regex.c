/*
 * Regex — PCRE2-backed regular expressions for Yona.
 *
 * Compiled regex handles are RC-managed (RC_TYPE_REGEX).
 * Low-level C API used by the Yona Std\Regex module wrapper.
 *
 * find returns [full_match, group1, ...] or [] — the Yona wrapper
 * converts this to the Match/Result ADT.
 */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

extern void* rc_alloc(int64_t type_tag, size_t bytes);
extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);
extern int64_t* yona_rt_seq_alloc(int64_t count);

#define RC_TYPE_STRING 6
#define RC_TYPE_REGEX  16

typedef struct { pcre2_code* code; } yona_regex_t;

/* Called by rc_dec when a regex handle reaches refcount 0. */
void yona_regex_free_code(void* code) {
    pcre2_code_free_8((pcre2_code*)code);
}

static char* rc_string(const char* s, size_t len) {
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

/* ===== compile ===== */

void* yona_regex_compile(const char* pattern) {
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code* code = pcre2_compile_8(
        (PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED,
        PCRE2_UTF | PCRE2_UCP, &errcode, &erroffset, NULL);
    if (!code) {
        PCRE2_UCHAR8 errbuf[256];
        pcre2_get_error_message_8(errcode, errbuf, sizeof(errbuf));
        fprintf(stderr, "Regex error at offset %zu: %s\n", (size_t)erroffset, errbuf);
        return NULL;
    }
    pcre2_jit_compile_8(code, PCRE2_JIT_COMPLETE);
    yona_regex_t* re = (yona_regex_t*)rc_alloc(RC_TYPE_REGEX, sizeof(yona_regex_t));
    re->code = code;
    return re;
}

/* ===== matches ===== */

int64_t yona_regex_matches(void* regex, const char* text) {
    if (!regex || !text) return 0;
    yona_regex_t* re = (yona_regex_t*)regex;
    pcre2_match_data* md = pcre2_match_data_create_from_pattern_8(re->code, NULL);
    int rc = pcre2_match_8(re->code, (PCRE2_SPTR8)text, PCRE2_ZERO_TERMINATED,
                            0, 0, md, NULL);
    pcre2_match_data_free_8(md);
    return rc >= 0 ? 1 : 0;
}

/* ===== Build match seq: [full_match, group1, group2, ...] ===== */

static int64_t* build_match_seq(pcre2_match_data* md, const char* text, int rc) {
    PCRE2_SIZE* ov = pcre2_get_ovector_pointer_8(md);
    int ngroups = rc;
    int64_t* seq = yona_rt_seq_alloc(ngroups);
    seq[1] = 1; /* heap_flag: elements are strings */
    for (int i = 0; i < ngroups; i++) {
        PCRE2_SIZE gs = ov[i * 2], ge = ov[i * 2 + 1];
        char* s;
        if (gs == PCRE2_UNSET || gs > ge)
            s = rc_string("", 0);
        else
            s = rc_string(text + gs, (size_t)(ge - gs));
        seq[2 + i] = (int64_t)(intptr_t)s;
    }
    return seq;
}

/* ===== find: returns [matched, g1, ...] or [] ===== */

int64_t* yona_regex_find(void* regex, const char* text) {
    if (!regex || !text) return yona_rt_seq_alloc(0);
    yona_regex_t* re = (yona_regex_t*)regex;
    pcre2_match_data* md = pcre2_match_data_create_from_pattern_8(re->code, NULL);
    int rc = pcre2_match_8(re->code, (PCRE2_SPTR8)text, PCRE2_ZERO_TERMINATED,
                            0, 0, md, NULL);
    if (rc < 0) { pcre2_match_data_free_8(md); return yona_rt_seq_alloc(0); }
    int64_t* result = build_match_seq(md, text, rc);
    pcre2_match_data_free_8(md);
    return result;
}

/* ===== findAll: returns seq of match-seqs ===== */

int64_t* yona_regex_findAll(void* regex, const char* text) {
    if (!regex || !text) return yona_rt_seq_alloc(0);
    yona_regex_t* re = (yona_regex_t*)regex;
    size_t textlen = strlen(text);
    pcre2_match_data* md = pcre2_match_data_create_from_pattern_8(re->code, NULL);

    int cap = 16;
    int64_t** matches = (int64_t**)malloc((size_t)cap * sizeof(int64_t*));
    int count = 0;
    PCRE2_SIZE offset = 0;

    while (offset <= textlen) {
        int rc = pcre2_match_8(re->code, (PCRE2_SPTR8)text, textlen,
                                offset, 0, md, NULL);
        if (rc < 0) break;
        if (count >= cap) { cap *= 2; matches = (int64_t**)realloc(matches, (size_t)cap * sizeof(int64_t*)); }
        matches[count++] = build_match_seq(md, text, rc);
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer_8(md);
        offset = ov[1];
        if (ov[0] == ov[1]) offset++;
    }

    int64_t* result = yona_rt_seq_alloc(count);
    result[1] = 1;
    for (int i = 0; i < count; i++)
        result[2 + i] = (int64_t)(intptr_t)matches[i];
    free(matches);
    pcre2_match_data_free_8(md);
    return result;
}

/* ===== replace / replaceAll ===== */

static char* regex_replace_impl(void* regex, const char* text,
                                 const char* replacement, int global) {
    if (!regex || !text || !replacement)
        return rc_string(text ? text : "", text ? strlen(text) : 0);
    yona_regex_t* re = (yona_regex_t*)regex;
    uint32_t opts = PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
    if (global) opts |= PCRE2_SUBSTITUTE_GLOBAL;

    pcre2_match_data* md = pcre2_match_data_create_from_pattern_8(re->code, NULL);
    PCRE2_SIZE outlen = 0;
    int rc = pcre2_substitute_8(re->code, (PCRE2_SPTR8)text, PCRE2_ZERO_TERMINATED,
                                 0, opts, md, NULL,
                                 (PCRE2_SPTR8)replacement, PCRE2_ZERO_TERMINATED,
                                 NULL, &outlen);
    if (rc != PCRE2_ERROR_NOMEMORY && rc < 0) {
        pcre2_match_data_free_8(md);
        return rc_string(text, strlen(text));
    }
    outlen++;
    char* output = (char*)rc_alloc(RC_TYPE_STRING, outlen);
    PCRE2_SIZE actual = outlen;
    rc = pcre2_substitute_8(re->code, (PCRE2_SPTR8)text, PCRE2_ZERO_TERMINATED,
                             0, opts & ~PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, md, NULL,
                             (PCRE2_SPTR8)replacement, PCRE2_ZERO_TERMINATED,
                             (PCRE2_UCHAR8*)output, &actual);
    pcre2_match_data_free_8(md);
    if (rc < 0) return rc_string(text, strlen(text));
    output[actual] = '\0';
    return output;
}

char* yona_regex_replace(void* regex, const char* text, const char* repl) {
    return regex_replace_impl(regex, text, repl, 0);
}

char* yona_regex_replaceAll(void* regex, const char* text, const char* repl) {
    return regex_replace_impl(regex, text, repl, 1);
}

/* ===== split ===== */

int64_t* yona_regex_split(void* regex, const char* text) {
    if (!regex || !text) return yona_rt_seq_alloc(0);
    yona_regex_t* re = (yona_regex_t*)regex;
    size_t textlen = strlen(text);
    pcre2_match_data* md = pcre2_match_data_create_from_pattern_8(re->code, NULL);

    int cap = 16;
    char** parts = (char**)malloc((size_t)cap * sizeof(char*));
    int count = 0;
    PCRE2_SIZE prev_end = 0, offset = 0;

    while (offset <= textlen) {
        int rc = pcre2_match_8(re->code, (PCRE2_SPTR8)text, textlen,
                                offset, 0, md, NULL);
        if (rc < 0) break;
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer_8(md);
        if (count >= cap) { cap *= 2; parts = (char**)realloc(parts, (size_t)cap * sizeof(char*)); }
        parts[count++] = rc_string(text + prev_end, (size_t)(ov[0] - prev_end));
        prev_end = ov[1];
        offset = ov[1];
        if (ov[0] == ov[1]) offset++;
    }
    if (count >= cap) { cap++; parts = (char**)realloc(parts, (size_t)cap * sizeof(char*)); }
    parts[count++] = rc_string(text + prev_end, textlen - (size_t)prev_end);

    int64_t* result = yona_rt_seq_alloc(count);
    result[1] = 1;
    for (int i = 0; i < count; i++)
        result[2 + i] = (int64_t)(intptr_t)parts[i];
    free(parts);
    pcre2_match_data_free_8(md);
    return result;
}

/* Public Std\Regex ABI. Regex is an opaque managed ADT at the language
 * boundary; its payload is the RC_TYPE_REGEX runtime object itself. */
void* yona_Std_Regex__compile(const char* pattern) {
    return yona_regex_compile(pattern);
}

int64_t yona_Std_Regex__matches(void* regex, const char* text) {
    return yona_regex_matches(regex, text);
}

int64_t* yona_Std_Regex__find(void* regex, const char* text) {
    return yona_regex_find(regex, text);
}

int64_t* yona_Std_Regex__findAll(void* regex, const char* text) {
    return yona_regex_findAll(regex, text);
}

char* yona_Std_Regex__replace(void* regex, const char* text, const char* replacement) {
    return yona_regex_replace(regex, text, replacement);
}

char* yona_Std_Regex__replaceAll(void* regex, const char* text, const char* replacement) {
    return yona_regex_replaceAll(regex, text, replacement);
}

int64_t* yona_Std_Regex__split(void* regex, const char* text) {
    return yona_regex_split(regex, text);
}
