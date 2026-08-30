/*
 * Regex — PCRE2-backed regular expressions for Yona.
 *
 * Compiled regex handles are RC-managed (RC_TYPE_REGEX).
 * Low-level C API used by the Yona Std\Regex module wrapper.
 *
 * find returns [full_match, group1, ...] or [] — the Yona wrapper
 * converts this to the Match/Result ADT.
 */

#include "yona/Runtime/Codecs/Regex.h"

#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YONA_RC_TYPE_STRING 6
#define YONA_RC_TYPE_REGEX 16

typedef struct {
  pcre2_code *Code;
} YonaRegex;

static void releaseMatchList(int64_t **Matches, int Count) {
  for (int I = 0; I < Count; I++)
    YonaRuntimeRelease(Matches[I]);
  free(Matches);
}

static void releaseStringList(char **Strings, int Count) {
  for (int I = 0; I < Count; I++)
    YonaRuntimeRelease(Strings[I]);
  free(Strings);
}

/* Called by rc_dec when a regex handle reaches refcount 0. */
void YonaRuntimeRegexDisposeCompiledCode(void *Code) {
  pcre2_code_free_8((pcre2_code *)Code);
}

void YonaRuntimeRegexRetain(void *Value) {
  if (Value)
    YonaRuntimeRetain(Value);
}

void YonaRuntimeRegexRelease(void *Value) {
  if (Value)
    YonaRuntimeRelease(Value);
}

static char *rcString(const char *S, size_t Len) {
  char *R = (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, Len + 1);
  memcpy(R, S, Len);
  R[Len] = '\0';
  return R;
}

/* ===== compile ===== */

YonaRegexRef YonaStdRegexCompile(const char *Pattern) {
  if (!Pattern)
    return NULL;
  int Errcode;
  PCRE2_SIZE Erroffset;
  pcre2_code *Code =
      pcre2_compile_8((PCRE2_SPTR8)Pattern, PCRE2_ZERO_TERMINATED,
                      PCRE2_UTF | PCRE2_UCP, &Errcode, &Erroffset, NULL);
  if (!Code) {
    PCRE2_UCHAR8 Errbuf[256];
    pcre2_get_error_message_8(Errcode, Errbuf, sizeof(Errbuf));
    fprintf(stderr, "Regex error at offset %zu: %s\n", (size_t)Erroffset,
            Errbuf);
    return NULL;
  }
  pcre2_jit_compile_8(Code, PCRE2_JIT_COMPLETE);
  YonaRegex *Re =
      (YonaRegex *)YonaRuntimeAllocate(YONA_RC_TYPE_REGEX, sizeof(YonaRegex));
  Re->Code = Code;
  return (YonaRegexRef)Re;
}

/* ===== matches ===== */

int64_t YonaStdRegexMatches(YonaRegexRef Regex, const char *Text) {
  if (!Regex || !Text)
    return 0;
  YonaRegex *Re = (YonaRegex *)Regex;
  pcre2_match_data *Md = pcre2_match_data_create_from_pattern_8(Re->Code, NULL);
  if (!Md)
    return 0;
  int Rc = pcre2_match_8(Re->Code, (PCRE2_SPTR8)Text, PCRE2_ZERO_TERMINATED, 0,
                         0, Md, NULL);
  pcre2_match_data_free_8(Md);
  return Rc >= 0 ? 1 : 0;
}

/* ===== Build match seq: [full_match, group1, group2, ...] ===== */

static int64_t *buildMatchSeq(pcre2_match_data *Md, const char *Text, int Rc) {
  PCRE2_SIZE *Ov = pcre2_get_ovector_pointer_8(Md);
  int Ngroups = Rc;
  int64_t *Seq = YonaRuntimeSequenceAllocate(Ngroups);
  Seq[1] = 1; /* heap_flag: elements are strings */
  for (int I = 0; I < Ngroups; I++) {
    PCRE2_SIZE Gs = Ov[I * 2], Ge = Ov[I * 2 + 1];
    char *S;
    if (Gs == PCRE2_UNSET || Gs > Ge)
      S = rcString("", 0);
    else
      S = rcString(Text + Gs, (size_t)(Ge - Gs));
    Seq[2 + I] = (int64_t)(intptr_t)S;
  }
  return Seq;
}

/* ===== find: returns [matched, g1, ...] or [] ===== */

int64_t *YonaStdRegexFind(YonaRegexRef Regex, const char *Text) {
  if (!Regex || !Text)
    return YonaRuntimeSequenceAllocate(0);
  YonaRegex *Re = (YonaRegex *)Regex;
  pcre2_match_data *Md = pcre2_match_data_create_from_pattern_8(Re->Code, NULL);
  if (!Md)
    return YonaRuntimeSequenceAllocate(0);
  int Rc = pcre2_match_8(Re->Code, (PCRE2_SPTR8)Text, PCRE2_ZERO_TERMINATED, 0,
                         0, Md, NULL);
  if (Rc < 0) {
    pcre2_match_data_free_8(Md);
    return YonaRuntimeSequenceAllocate(0);
  }
  int64_t *Result = buildMatchSeq(Md, Text, Rc);
  pcre2_match_data_free_8(Md);
  return Result;
}

/* ===== findAll: returns seq of match-seqs ===== */

int64_t *YonaStdRegexFindAll(YonaRegexRef Regex, const char *Text) {
  if (!Regex || !Text)
    return YonaRuntimeSequenceAllocate(0);
  YonaRegex *Re = (YonaRegex *)Regex;
  size_t Textlen = strlen(Text);
  pcre2_match_data *Md = pcre2_match_data_create_from_pattern_8(Re->Code, NULL);
  if (!Md)
    return YonaRuntimeSequenceAllocate(0);

  int Cap = 16;
  int64_t **Matches = (int64_t **)malloc((size_t)Cap * sizeof(int64_t *));
  if (!Matches) {
    pcre2_match_data_free_8(Md);
    return YonaRuntimeSequenceAllocate(0);
  }
  int Count = 0;
  PCRE2_SIZE Offset = 0;

  while (Offset <= Textlen) {
    int Rc = pcre2_match_8(Re->Code, (PCRE2_SPTR8)Text, Textlen, Offset, 0, Md,
                           NULL);
    if (Rc < 0)
      break;
    if (Count >= Cap) {
      int NextCap = Cap * 2;
      int64_t **Grown =
          (int64_t **)realloc(Matches, (size_t)NextCap * sizeof(int64_t *));
      if (!Grown) {
        releaseMatchList(Matches, Count);
        pcre2_match_data_free_8(Md);
        return YonaRuntimeSequenceAllocate(0);
      }
      Matches = Grown;
      Cap = NextCap;
    }
    Matches[Count++] = buildMatchSeq(Md, Text, Rc);
    PCRE2_SIZE *Ov = pcre2_get_ovector_pointer_8(Md);
    Offset = Ov[1];
    if (Ov[0] == Ov[1])
      Offset++;
  }

  int64_t *Result = YonaRuntimeSequenceAllocate(Count);
  Result[1] = 1;
  for (int I = 0; I < Count; I++)
    Result[2 + I] = (int64_t)(intptr_t)Matches[I];
  free(Matches);
  pcre2_match_data_free_8(Md);
  return Result;
}

/* ===== replace / replaceAll ===== */

static char *regexReplaceImpl(void *Regex, const char *Text,
                              const char *Replacement, int Global) {
  if (!Regex || !Text || !Replacement)
    return rcString(Text ? Text : "", Text ? strlen(Text) : 0);
  YonaRegex *Re = (YonaRegex *)Regex;
  uint32_t Opts = PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
  if (Global)
    Opts |= PCRE2_SUBSTITUTE_GLOBAL;

  pcre2_match_data *Md = pcre2_match_data_create_from_pattern_8(Re->Code, NULL);
  if (!Md)
    return rcString(Text, strlen(Text));
  PCRE2_SIZE Outlen = 0;
  int Rc = pcre2_substitute_8(
      Re->Code, (PCRE2_SPTR8)Text, PCRE2_ZERO_TERMINATED, 0, Opts, Md, NULL,
      (PCRE2_SPTR8)Replacement, PCRE2_ZERO_TERMINATED, NULL, &Outlen);
  if (Rc != PCRE2_ERROR_NOMEMORY && Rc < 0) {
    pcre2_match_data_free_8(Md);
    return rcString(Text, strlen(Text));
  }
  Outlen++;
  char *Output = (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, Outlen);
  PCRE2_SIZE Actual = Outlen;
  Rc = pcre2_substitute_8(Re->Code, (PCRE2_SPTR8)Text, PCRE2_ZERO_TERMINATED, 0,
                          Opts & ~PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, Md, NULL,
                          (PCRE2_SPTR8)Replacement, PCRE2_ZERO_TERMINATED,
                          (PCRE2_UCHAR8 *)Output, &Actual);
  pcre2_match_data_free_8(Md);
  if (Rc < 0) {
    YonaRuntimeRelease(Output);
    return rcString(Text, strlen(Text));
  }
  Output[Actual] = '\0';
  return Output;
}

char *YonaStdRegexReplace(YonaRegexRef Regex, const char *Text,
                          const char *Repl) {
  return regexReplaceImpl(Regex, Text, Repl, 0);
}

char *YonaStdRegexReplaceAll(YonaRegexRef Regex, const char *Text,
                             const char *Repl) {
  return regexReplaceImpl(Regex, Text, Repl, 1);
}

/* ===== split ===== */

int64_t *YonaStdRegexSplit(YonaRegexRef Regex, const char *Text) {
  if (!Regex || !Text)
    return YonaRuntimeSequenceAllocate(0);
  YonaRegex *Re = (YonaRegex *)Regex;
  size_t Textlen = strlen(Text);
  pcre2_match_data *Md = pcre2_match_data_create_from_pattern_8(Re->Code, NULL);
  if (!Md)
    return YonaRuntimeSequenceAllocate(0);

  int Cap = 16;
  char **Parts = (char **)malloc((size_t)Cap * sizeof(char *));
  if (!Parts) {
    pcre2_match_data_free_8(Md);
    return YonaRuntimeSequenceAllocate(0);
  }
  int Count = 0;
  PCRE2_SIZE PrevEnd = 0, Offset = 0;

  while (Offset <= Textlen) {
    int Rc = pcre2_match_8(Re->Code, (PCRE2_SPTR8)Text, Textlen, Offset, 0, Md,
                           NULL);
    if (Rc < 0)
      break;
    PCRE2_SIZE *Ov = pcre2_get_ovector_pointer_8(Md);
    if (Count >= Cap) {
      int NextCap = Cap * 2;
      char **Grown = (char **)realloc(Parts, (size_t)NextCap * sizeof(char *));
      if (!Grown) {
        releaseStringList(Parts, Count);
        pcre2_match_data_free_8(Md);
        return YonaRuntimeSequenceAllocate(0);
      }
      Parts = Grown;
      Cap = NextCap;
    }
    Parts[Count++] = rcString(Text + PrevEnd, (size_t)(Ov[0] - PrevEnd));
    PrevEnd = Ov[1];
    Offset = Ov[1];
    if (Ov[0] == Ov[1])
      Offset++;
  }
  if (Count >= Cap) {
    char **Grown = (char **)realloc(Parts, (size_t)(Cap + 1) * sizeof(char *));
    if (!Grown) {
      releaseStringList(Parts, Count);
      pcre2_match_data_free_8(Md);
      return YonaRuntimeSequenceAllocate(0);
    }
    Parts = Grown;
    Cap++;
  }
  Parts[Count++] = rcString(Text + PrevEnd, Textlen - (size_t)PrevEnd);

  int64_t *Result = YonaRuntimeSequenceAllocate(Count);
  Result[1] = 1;
  for (int I = 0; I < Count; I++)
    Result[2 + I] = (int64_t)(intptr_t)Parts[I];
  free(Parts);
  pcre2_match_data_free_8(Md);
  return Result;
}
