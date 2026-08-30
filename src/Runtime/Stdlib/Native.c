/*
 * Native standard-library entry points.
 *
 * This component owns native bindings that require system access, mutable
 * primitives, external libraries, or measured hot loops. Pure transformations
 * belong in Yona source modules.
 */

#if defined(_WIN32)
#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif
#endif

#include "Runtime/Core/Internal.h"
#include "yona/Runtime/Codecs/Regex.h"
#include "yona/Runtime/Collections/Arrays.h"
#include "yona/Runtime/Collections/Dictionary.h"
#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Collections/Set.h"
#include "yona/Runtime/Concurrency/Channel.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include "yona/Runtime/Platform/Windows.h"

#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__has_include)
#if __has_include("yona/Support/Version.h")
#include "yona/Support/Version.h"
#endif
#endif

#ifndef YONA_VERSION_STRING
#define YONA_VERSION_STRING "unknown"
#endif

#define YONA_STDLIB_ADT_HEADER_SIZE 3

/* Exceptions: setjmp/longjmp-based try/catch/raise */
/* ===== Native stdlib shims ===== */
/* Pure C implementations of common stdlib functions for compiled code. */
/* These use the same mangled names as compiled Yona modules. */

/* Std\Math */
int64_t YonaStdMathAbs(int64_t X) { return X < 0 ? -X : X; }
int64_t YonaStdMathMax(int64_t A, int64_t B) { return A > B ? A : B; }
int64_t YonaStdMathMin(int64_t A, int64_t B) { return A < B ? A : B; }
int64_t YonaStdMathFactorial(int64_t N) {
  int64_t R = 1;
  for (int64_t I = 2; I <= N; I++)
    R *= I;
  return R;
}
double YonaStdMathSqrt(double X) { return sqrt(X); }
double YonaStdMathSin(double X) { return sin(X); }
double YonaStdMathCos(double X) { return cos(X); }

/* Std\String — pure string operations, no I/O */

#define YONA_UTF8_REPLACEMENT 0xfffdu

/* Decode one Unicode scalar. Invalid UTF-8 consumes one byte and produces
 * U+FFFD, giving native inputs deterministic forward progress. */
static size_t yonaStringUtf8Decode(const char *Input, size_t Remaining,
                                   uint32_t *Scalar) {
  if (!Remaining) {
    *Scalar = YONA_UTF8_REPLACEMENT;
    return 0;
  }
  const unsigned char *S = (const unsigned char *)Input;
  if (S[0] < 0x80) {
    *Scalar = S[0];
    return 1;
  }
  if (S[0] >= 0xc2 && S[0] <= 0xdf && Remaining >= 2 && (S[1] & 0xc0) == 0x80) {
    *Scalar = ((uint32_t)(S[0] & 0x1f) << 6) | (uint32_t)(S[1] & 0x3f);
    return 2;
  }
  if (S[0] >= 0xe0 && S[0] <= 0xef && Remaining >= 3 && (S[1] & 0xc0) == 0x80 &&
      (S[2] & 0xc0) == 0x80 && !(S[0] == 0xe0 && S[1] < 0xa0) &&
      !(S[0] == 0xed && S[1] >= 0xa0)) {
    *Scalar = ((uint32_t)(S[0] & 0x0f) << 12) | ((uint32_t)(S[1] & 0x3f) << 6) |
              (uint32_t)(S[2] & 0x3f);
    return 3;
  }
  if (S[0] >= 0xf0 && S[0] <= 0xf4 && Remaining >= 4 && (S[1] & 0xc0) == 0x80 &&
      (S[2] & 0xc0) == 0x80 && (S[3] & 0xc0) == 0x80 &&
      !(S[0] == 0xf0 && S[1] < 0x90) && !(S[0] == 0xf4 && S[1] >= 0x90)) {
    *Scalar = ((uint32_t)(S[0] & 0x07) << 18) |
              ((uint32_t)(S[1] & 0x3f) << 12) | ((uint32_t)(S[2] & 0x3f) << 6) |
              (uint32_t)(S[3] & 0x3f);
    return 4;
  }
  *Scalar = YONA_UTF8_REPLACEMENT;
  return 1;
}

static size_t yonaUtf8EncodedSize(uint32_t Scalar) {
  if (Scalar == 0 || Scalar > 0x10ffff ||
      (Scalar >= 0xd800 && Scalar <= 0xdfff))
    Scalar = YONA_UTF8_REPLACEMENT;
  if (Scalar <= 0x7f)
    return 1;
  if (Scalar <= 0x7ff)
    return 2;
  if (Scalar <= 0xffff)
    return 3;
  return 4;
}

static size_t yonaUtf8Encode(char *Output, uint32_t Scalar) {
  if (Scalar == 0 || Scalar > 0x10ffff ||
      (Scalar >= 0xd800 && Scalar <= 0xdfff))
    Scalar = YONA_UTF8_REPLACEMENT;
  if (Scalar <= 0x7f) {
    Output[0] = (char)Scalar;
    return 1;
  }
  if (Scalar <= 0x7ff) {
    Output[0] = (char)(0xc0 | (Scalar >> 6));
    Output[1] = (char)(0x80 | (Scalar & 0x3f));
    return 2;
  }
  if (Scalar <= 0xffff) {
    Output[0] = (char)(0xe0 | (Scalar >> 12));
    Output[1] = (char)(0x80 | ((Scalar >> 6) & 0x3f));
    Output[2] = (char)(0x80 | (Scalar & 0x3f));
    return 3;
  }
  Output[0] = (char)(0xf0 | (Scalar >> 18));
  Output[1] = (char)(0x80 | ((Scalar >> 12) & 0x3f));
  Output[2] = (char)(0x80 | ((Scalar >> 6) & 0x3f));
  Output[3] = (char)(0x80 | (Scalar & 0x3f));
  return 4;
}

static size_t yonaUtf8ScalarCountBytes(const char *S, size_t Bytes) {
  size_t Count = 0;
  for (size_t Offset = 0; Offset < Bytes; ++Count) {
    uint32_t Scalar;
    size_t Consumed = yonaStringUtf8Decode(S + Offset, Bytes - Offset, &Scalar);
    Offset += Consumed ? Consumed : 1;
  }
  return Count;
}

static size_t yonaUtf8ScalarCount(const char *S) {
  return yonaUtf8ScalarCountBytes(S, strlen(S));
}

static size_t yonaUtf8ByteOffset(const char *S, size_t ScalarIndex) {
  const size_t Bytes = strlen(S);
  size_t Offset = 0;
  for (size_t Index = 0; Offset < Bytes && Index < ScalarIndex; ++Index) {
    uint32_t Scalar;
    size_t Consumed = yonaStringUtf8Decode(S + Offset, Bytes - Offset, &Scalar);
    Offset += Consumed ? Consumed : 1;
  }
  return Offset;
}

int64_t YonaStdStringLength(const char *S) {
  return (int64_t)yonaUtf8ScalarCount(S);
}

const char *YonaStdStringToUpperCase(const char *S) {
  size_t Len = strlen(S);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  for (size_t I = 0; I <= Len; I++)
    R[I] = (char)toupper((unsigned char)S[I]);
  return R;
}

const char *YonaStdStringToLowerCase(const char *S) {
  size_t Len = strlen(S);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  for (size_t I = 0; I <= Len; I++)
    R[I] = (char)tolower((unsigned char)S[I]);
  return R;
}

const char *YonaStdStringTrim(const char *S) {
  if (!*S) {
    char *Empty = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 1);
    Empty[0] = '\0';
    return Empty;
  }
  const char *Start = S;
  while (*Start &&
         (*Start == ' ' || *Start == '\t' || *Start == '\n' || *Start == '\r'))
    Start++;
  const char *End = S + strlen(S) - 1;
  while (End > Start &&
         (*End == ' ' || *End == '\t' || *End == '\n' || *End == '\r'))
    End--;
  size_t Len = End - Start + 1;
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  memcpy(R, Start, Len);
  R[Len] = '\0';
  return R;
}

int64_t YonaStdStringIndexOf(const char *Needle, const char *Haystack) {
  const char *P = strstr(Haystack, Needle);
  return P ? (int64_t)yonaUtf8ScalarCountBytes(Haystack, (size_t)(P - Haystack))
           : -1;
}

int64_t YonaStdStringContains(const char *Needle, const char *Haystack) {
  return strstr(Haystack, Needle) != NULL;
}

int64_t YonaStdStringStartsWith(const char *Prefix, const char *S) {
  size_t Plen = strlen(Prefix);
  return strncmp(S, Prefix, Plen) == 0;
}

int64_t YonaStdStringEndsWith(const char *Suffix, const char *S) {
  size_t Slen = strlen(S);
  size_t Xlen = strlen(Suffix);
  if (Xlen > Slen)
    return 0;
  return strcmp(S + Slen - Xlen, Suffix) == 0;
}

const char *YonaStdStringSubstring(const char *S, int64_t Start, int64_t End) {
  if (Start < 0)
    Start = 0;
  if (End < Start)
    End = Start;
  if (End < 0)
    End = 0;
  const size_t StartByte = yonaUtf8ByteOffset(S, (size_t)Start);
  const size_t EndByte = yonaUtf8ByteOffset(S, (size_t)End);
  size_t Len = EndByte - StartByte;
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  memcpy(R, S + StartByte, Len);
  R[Len] = '\0';
  return R;
}

const char *YonaStdStringReplace(const char *S, const char *Old,
                                 const char *NewS) {
  size_t Olen = strlen(Old);
  size_t Nlen = strlen(NewS);
  size_t Slen = strlen(S);
  if (Olen == 0) {
    size_t L = strlen(S);
    char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, L + 1);
    memcpy(R, S, L + 1);
    return R;
  }
  /* Count occurrences */
  size_t Count = 0;
  const char *P = S;
  while ((P = strstr(P, Old)) != NULL) {
    Count++;
    P += Olen;
  }
  /* Build result */
  size_t Rlen = Slen + Count * (Nlen - Olen);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Rlen + 1);
  char *W = R;
  P = S;
  while (*P) {
    if (strncmp(P, Old, Olen) == 0) {
      memcpy(W, NewS, Nlen);
      W += Nlen;
      P += Olen;
    } else {
      *W++ = *P++;
    }
  }
  *W = '\0';
  return R;
}

/* split returns an Iterator that yields substrings on demand */
typedef struct {
  void (*Finalize)(void *);
  const char *Str;
  const char *Pos;
  const char *Delim;
  size_t Dlen;
  int Done;
} SplitIteratorState;

static void splitIterFinalize(void *Raw) {
  SplitIteratorState *St = (SplitIteratorState *)Raw;
  if (St->Str)
    YonaRuntimeRelease((void *)St->Str);
  if (St->Delim)
    YonaRuntimeRelease((void *)St->Delim);
}

static int64_t splitIterNext(int64_t *Env) {
  SplitIteratorState *St = (SplitIteratorState *)(intptr_t)Env[6];
  if (St->Done)
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();
  const char *Next = St->Dlen > 0 ? strstr(St->Pos, St->Delim) : NULL;
  size_t Len;
  if (St->Dlen == 0) {
    if (*St->Pos == '\0') {
      St->Done = 1;
      return (int64_t)(intptr_t)YonaRuntimeMakeNone();
    }
    uint32_t Scalar;
    Len = yonaStringUtf8Decode(St->Pos, strlen(St->Pos), &Scalar);
    Next = NULL;
  } else {
    Len = Next ? (size_t)(Next - St->Pos) : strlen(St->Pos);
  }
  size_t OutputLen = Len;
  uint32_t EmptyDelimiterScalar = 0;
  if (St->Dlen == 0) {
    yonaStringUtf8Decode(St->Pos, strlen(St->Pos), &EmptyDelimiterScalar);
    OutputLen = yonaUtf8EncodedSize(EmptyDelimiterScalar);
  }
  char *Part =
      (char *)YonaRuntimeAllocateStringWithLength(OutputLen + 1, OutputLen);
  if (St->Dlen == 0)
    yonaUtf8Encode(Part, EmptyDelimiterScalar);
  else
    memcpy(Part, St->Pos, Len);
  Part[OutputLen] = '\0';
  if (Next)
    St->Pos = Next + St->Dlen;
  else if (St->Dlen == 0)
    St->Pos += Len;
  else
    St->Done = 1;
  if (St->Dlen == 0 && *St->Pos == '\0')
    St->Done = 1;
  return (int64_t)(intptr_t)YonaRuntimeMakeSome((int64_t)(intptr_t)Part);
}

int64_t YonaStdStringSplit(const char *Delim, const char *S) {
  SplitIteratorState *St = (SplitIteratorState *)YonaRuntimeNativeStateAllocate(
      sizeof(SplitIteratorState), splitIterFinalize);
  St->Str = S;
  St->Pos = S;
  St->Delim = Delim;
  St->Dlen = strlen(Delim);
  St->Done = 0;
  YonaRuntimeRetain((void *)S);
  YonaRuntimeRetain((void *)Delim);
  int64_t *Cl =
      (int64_t *)YonaRuntimeClosureCreate((void *)splitIterNext, 0, 0, 1);
  YonaRuntimeClosureSetCapture(Cl, 0, (int64_t)(intptr_t)St);
  YonaRuntimeClosureSetHeapMask(Cl, 1);
  return (int64_t)(intptr_t)YonaRuntimeMakeIterator(Cl);
}

const char *YonaStdStringJoin(const char *Sep, int64_t *Seq) {
  int64_t N = Seq[0];
  if (N == 0) {
    char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 1);
    R[0] = '\0';
    return R;
  }
  size_t Seplen = strlen(Sep);
  /* Calculate total length */
  size_t Total = 0;
  for (int64_t I = 0; I < N; I++) {
    const char *Part = (const char *)(intptr_t)YonaRuntimeSequenceGet(Seq, I);
    Total += strlen(Part);
    if (I > 0)
      Total += Seplen;
  }
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Total + 1);
  char *W = R;
  for (int64_t I = 0; I < N; I++) {
    if (I > 0) {
      memcpy(W, Sep, Seplen);
      W += Seplen;
    }
    const char *Part = (const char *)(intptr_t)YonaRuntimeSequenceGet(Seq, I);
    size_t Plen = strlen(Part);
    memcpy(W, Part, Plen);
    W += Plen;
  }
  *W = '\0';
  return R;
}

int64_t YonaStdStringCharAt(const char *S, int64_t Idx) {
  if (Idx < 0)
    return 0;
  const size_t Bytes = strlen(S);
  size_t Offset = yonaUtf8ByteOffset(S, (size_t)Idx);
  if (Offset >= Bytes)
    return 0;
  uint32_t Scalar;
  yonaStringUtf8Decode(S + Offset, Bytes - Offset, &Scalar);
  return (int64_t)Scalar;
}

const char *YonaStdStringPadLeft(int64_t Width, const char *Pad,
                                 const char *S) {
  const size_t Sbytes = strlen(S);
  const size_t Scalars = yonaUtf8ScalarCountBytes(S, Sbytes);
  const size_t Pbytes = strlen(Pad);
  if (Width <= (int64_t)Scalars || Pbytes == 0) {
    char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Sbytes + 1);
    memcpy(R, S, Sbytes + 1);
    return R;
  }
  const size_t Fill = (size_t)Width - Scalars;
  size_t FillBytes = 0, PadOffset = 0;
  for (size_t I = 0; I < Fill; ++I) {
    if (PadOffset >= Pbytes)
      PadOffset = 0;
    uint32_t Scalar;
    size_t Consumed =
        yonaStringUtf8Decode(Pad + PadOffset, Pbytes - PadOffset, &Scalar);
    FillBytes += yonaUtf8EncodedSize(Scalar);
    PadOffset += Consumed ? Consumed : 1;
  }
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING,
                                        FillBytes + Sbytes + 1);
  char *Out = R;
  PadOffset = 0;
  for (size_t I = 0; I < Fill; ++I) {
    if (PadOffset >= Pbytes)
      PadOffset = 0;
    uint32_t Scalar;
    size_t Consumed =
        yonaStringUtf8Decode(Pad + PadOffset, Pbytes - PadOffset, &Scalar);
    Out += yonaUtf8Encode(Out, Scalar);
    PadOffset += Consumed ? Consumed : 1;
  }
  memcpy(Out, S, Sbytes + 1);
  return R;
}

const char *YonaStdStringPadRight(int64_t Width, const char *Pad,
                                  const char *S) {
  const size_t Sbytes = strlen(S);
  const size_t Scalars = yonaUtf8ScalarCountBytes(S, Sbytes);
  const size_t Pbytes = strlen(Pad);
  if (Width <= (int64_t)Scalars || Pbytes == 0) {
    char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Sbytes + 1);
    memcpy(R, S, Sbytes + 1);
    return R;
  }
  const size_t Fill = (size_t)Width - Scalars;
  size_t FillBytes = 0, PadOffset = 0;
  for (size_t I = 0; I < Fill; ++I) {
    if (PadOffset >= Pbytes)
      PadOffset = 0;
    uint32_t Scalar;
    size_t Consumed =
        yonaStringUtf8Decode(Pad + PadOffset, Pbytes - PadOffset, &Scalar);
    FillBytes += yonaUtf8EncodedSize(Scalar);
    PadOffset += Consumed ? Consumed : 1;
  }
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING,
                                        Sbytes + FillBytes + 1);
  memcpy(R, S, Sbytes);
  char *Out = R + Sbytes;
  PadOffset = 0;
  for (size_t I = 0; I < Fill; ++I) {
    if (PadOffset >= Pbytes)
      PadOffset = 0;
    uint32_t Scalar;
    size_t Consumed =
        yonaStringUtf8Decode(Pad + PadOffset, Pbytes - PadOffset, &Scalar);
    Out += yonaUtf8Encode(Out, Scalar);
    PadOffset += Consumed ? Consumed : 1;
  }
  *Out = '\0';
  return R;
}

const char *YonaStdStringReverse(const char *S) {
  const size_t Bytes = strlen(S);
  const size_t Count = yonaUtf8ScalarCountBytes(S, Bytes);
  uint32_t *Scalars =
      Count ? (uint32_t *)malloc(Count * sizeof(uint32_t)) : NULL;
  if (Count && !Scalars)
    abort();
  size_t Offset = 0, OutputBytes = 0;
  for (size_t I = 0; I < Count; ++I) {
    size_t Consumed =
        yonaStringUtf8Decode(S + Offset, Bytes - Offset, &Scalars[I]);
    Offset += Consumed ? Consumed : 1;
    OutputBytes += yonaUtf8EncodedSize(Scalars[I]);
  }
  char *R =
      (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, OutputBytes + 1);
  char *Out = R;
  for (size_t I = Count; I > 0; --I)
    Out += yonaUtf8Encode(Out, Scalars[I - 1]);
  *Out = '\0';
  free(Scalars);
  return R;
}

int64_t YonaStdStringToInt(const char *S) { return (int64_t)atoll(S); }
double YonaStdStringToFloat(const char *S) { return atof(S); }

int64_t YonaStdStringIsEmpty(const char *S) { return S[0] == '\0'; }

const char *YonaStdStringRepeat(int64_t N, const char *S) {
  size_t Len = strlen(S);
  size_t Total = Len * (size_t)N;
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Total + 1);
  for (int64_t I = 0; I < N; I++)
    memcpy(R + I * Len, S, Len);
  R[Total] = '\0';
  return R;
}

const char *YonaStdStringTake(int64_t N, const char *S) {
  if (N < 0)
    N = 0;
  const size_t Bytes = yonaUtf8ByteOffset(S, (size_t)N);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Bytes + 1);
  memcpy(R, S, Bytes);
  R[Bytes] = '\0';
  return R;
}

const char *YonaStdStringDrop(int64_t N, const char *S) {
  if (N < 0)
    N = 0;
  const size_t Len = strlen(S);
  const size_t Offset = yonaUtf8ByteOffset(S, (size_t)N);
  size_t NewLen = Len - Offset;
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, NewLen + 1);
  memcpy(R, S + Offset, NewLen + 1);
  return R;
}

int64_t YonaStdStringCount(const char *Needle, const char *Haystack) {
  if (Needle[0] == '\0')
    return 0;
  int64_t Count = 0;
  size_t Nlen = strlen(Needle);
  const char *P = Haystack;
  while ((P = strstr(P, Needle)) != NULL) {
    Count++;
    P += Nlen;
  }
  return Count;
}

int64_t YonaStdStringLines(const char *S) {
  /* Split by newline — returns Iterator */
  return YonaStdStringSplit("\n", S);
}

const char *YonaStdStringUnlines(int64_t *Seq) {
  return YonaStdStringJoin("\n", Seq);
}

/* chars returns an Iterator that yields each character as a single-char string
 */
typedef struct {
  void (*Finalize)(void *);
  const char *Str;
  size_t Pos;
  size_t Len;
} CharacterIteratorState;

static void charIterFinalize(void *Raw) {
  CharacterIteratorState *St = (CharacterIteratorState *)Raw;
  if (St->Str)
    YonaRuntimeRelease((void *)St->Str);
}

static int64_t charIterNext(int64_t *Env) {
  CharacterIteratorState *St = (CharacterIteratorState *)(intptr_t)Env[6];
  if (St->Pos >= St->Len)
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();
  uint32_t Scalar;
  size_t Consumed =
      yonaStringUtf8Decode(St->Str + St->Pos, St->Len - St->Pos, &Scalar);
  St->Pos += Consumed ? Consumed : 1;
  int64_t Ch = (int64_t)Scalar;
  return (int64_t)(intptr_t)YonaRuntimeMakeSome(Ch);
}

int64_t YonaStdStringChars(const char *S) {
  size_t Len = YonaRuntimeStringLength(S);
  CharacterIteratorState *St =
      (CharacterIteratorState *)YonaRuntimeNativeStateAllocate(
          sizeof(CharacterIteratorState), charIterFinalize);
  St->Str = S;
  St->Pos = 0;
  St->Len = Len;
  YonaRuntimeRetain((void *)S);
  int64_t *Cl =
      (int64_t *)YonaRuntimeClosureCreate((void *)charIterNext, 0, 0, 1);
  YonaRuntimeClosureSetCapture(Cl, 0, (int64_t)(intptr_t)St);
  YonaRuntimeClosureSetHeapMask(Cl, 1);
  return (int64_t)(intptr_t)YonaRuntimeMakeIterator(Cl);
}

const char *YonaStdStringFromChars(int64_t *Seq) {
  const int64_t Len = YonaRuntimeSequenceLength(Seq);
  size_t Bytes = 0;
  for (int64_t I = 0; I < Len; I++)
    Bytes += yonaUtf8EncodedSize((uint32_t)YonaRuntimeSequenceGet(Seq, I));
  char *R = (char *)YonaRuntimeAllocateStringWithLength(Bytes + 1, Bytes);
  char *Out = R;
  for (int64_t I = 0; I < Len; I++)
    Out += yonaUtf8Encode(Out, (uint32_t)YonaRuntimeSequenceGet(Seq, I));
  *Out = '\0';
  return R;
}

/* ===== Std\Convert — checked conversion and parsing primitives ===== */

static int64_t yonaConversionResultOk(int64_t Value, int ValueIsHeap) {
  int64_t *Result = (int64_t *)YonaRuntimeAdtAllocate(0, 1);
  YonaRuntimeAdtSetField(Result, 0, Value);
  YonaRuntimeAdtSetHeapMask(Result, ValueIsHeap ? 1 : 0);
  return (int64_t)(intptr_t)Result;
}

static char *yonaConversionCopyText(const char *Text) {
  const size_t Length = strlen(Text);
  char *Copy = (char *)YonaRuntimeAllocateStringWithLength(Length + 1, Length);
  memcpy(Copy, Text, Length + 1);
  return Copy;
}

static int64_t yonaConversionResultError(int ErrorTag, const char *Message) {
  int64_t *Error = (int64_t *)YonaRuntimeAdtAllocate(ErrorTag, 1);
  YonaRuntimeAdtSetField(Error, 0,
                         (int64_t)(intptr_t)yonaConversionCopyText(Message));
  YonaRuntimeAdtSetHeapMask(Error, 1);
  int64_t *Result = (int64_t *)YonaRuntimeAdtAllocate(1, 1);
  YonaRuntimeAdtSetField(Result, 0, (int64_t)(intptr_t)Error);
  YonaRuntimeAdtSetHeapMask(Result, 1);
  return (int64_t)(intptr_t)Result;
}

static int64_t yonaParseError(int ErrorTag, const char *Expected,
                              const char *Input) {
  char Message[320];
  snprintf(Message, sizeof(Message), "expected %s, found '%s'", Expected,
           Input);
  return yonaConversionResultError(ErrorTag, Message);
}

int64_t YonaStdConvertParseIntNative(const char *Text) {
  errno = 0;
  char *End = NULL;
  long long Value = strtoll(Text, &End, 10);
  if (End == Text)
    return yonaParseError(0, "a signed decimal Int", Text);
  while (*End && isspace((unsigned char)*End))
    ++End;
  if (*End)
    return yonaParseError(0, "a complete signed decimal Int", Text);
  if (errno == ERANGE)
    return yonaParseError(1, "an Int in the signed 64-bit range", Text);
  return yonaConversionResultOk((int64_t)Value, 0);
}

int64_t YonaStdConvertParseFloatNative(const char *Text) {
  errno = 0;
  char *End = NULL;
  double Value = strtod(Text, &End);
  if (End == Text)
    return yonaParseError(0, "a decimal Float", Text);
  while (*End && isspace((unsigned char)*End))
    ++End;
  if (*End)
    return yonaParseError(0, "a complete decimal Float", Text);
  if (errno == ERANGE || !isfinite(Value))
    return yonaParseError(1, "a finite Float", Text);
  int64_t Bits;
  memcpy(&Bits, &Value, sizeof(Bits));
  return yonaConversionResultOk(Bits, 0);
}

int64_t YonaStdConvertIntToFloatNative(int64_t Value) {
  const int64_t ExactLimit = INT64_C(9007199254740992);
  if (Value < -ExactLimit || Value > ExactLimit)
    return yonaConversionResultError(
        1, "Int is outside Float's exact integer range [-2^53, 2^53]");
  const double Converted = (double)Value;
  int64_t Bits;
  memcpy(&Bits, &Converted, sizeof(Bits));
  return yonaConversionResultOk(Bits, 0);
}

int64_t YonaStdConvertFloatToIntNative(double Value) {
  if (!isfinite(Value) || Value < -9223372036854775808.0 ||
      Value >= 9223372036854775808.0)
    return yonaConversionResultError(
        1, "Float is outside the signed 64-bit Int range");
  if (trunc(Value) != Value)
    return yonaConversionResultError(
        0, "Float has a fractional part; an integral value is required");
  return yonaConversionResultOk((int64_t)Value, 0);
}

int64_t YonaStdConvertDecodeUtf8Native(void *Bytes) {
  const int64_t SignedLength = YonaRuntimeByteArrayLength(Bytes);
  const size_t Length = SignedLength > 0 ? (size_t)SignedLength : 0;
  const char *Data = (const char *)((int64_t *)Bytes + 1);
  for (size_t Offset = 0; Offset < Length;) {
    uint32_t Scalar;
    size_t Consumed =
        yonaStringUtf8Decode(Data + Offset, Length - Offset, &Scalar);
    if (!Consumed || (Scalar == YONA_UTF8_REPLACEMENT && Consumed == 1))
      return yonaConversionResultError(0, "ByteArray is not well-formed UTF-8");
    if (Scalar == 0)
      return yonaConversionResultError(
          0, "Yona String cannot contain an embedded NUL scalar");
    Offset += Consumed;
  }
  char *Result =
      (char *)YonaRuntimeAllocateStringWithLength(Length + 1, Length);
  memcpy(Result, Data, Length);
  Result[Length] = '\0';
  return yonaConversionResultOk((int64_t)(intptr_t)Result, 1);
}

/* Std\Encoding — base64, hex, URL encoding */

static const char B64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const char *YonaStdEncodingBase64Encode(const char *S) {
  size_t Len = strlen(S);
  size_t OutLen = 4 * ((Len + 2) / 3);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, OutLen + 1);
  size_t J = 0;
  for (size_t I = 0; I < Len; I += 3) {
    uint32_t A = (uint8_t)S[I];
    uint32_t B = (I + 1 < Len) ? (uint8_t)S[I + 1] : 0;
    uint32_t C = (I + 2 < Len) ? (uint8_t)S[I + 2] : 0;
    uint32_t Triple = (A << 16) | (B << 8) | C;
    R[J++] = B64Table[(Triple >> 18) & 0x3F];
    R[J++] = B64Table[(Triple >> 12) & 0x3F];
    R[J++] = (I + 1 < Len) ? B64Table[(Triple >> 6) & 0x3F] : '=';
    R[J++] = (I + 2 < Len) ? B64Table[Triple & 0x3F] : '=';
  }
  R[J] = '\0';
  return R;
}

static int b64DecodeChar(char C) {
  if (C >= 'A' && C <= 'Z')
    return C - 'A';
  if (C >= 'a' && C <= 'z')
    return C - 'a' + 26;
  if (C >= '0' && C <= '9')
    return C - '0' + 52;
  if (C == '+')
    return 62;
  if (C == '/')
    return 63;
  return -1;
}

const char *YonaStdEncodingBase64Decode(const char *S) {
  size_t Len = strlen(S);
  if (Len % 4 != 0) {
    char *Empty = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 1);
    Empty[0] = '\0';
    return Empty;
  }
  for (size_t I = 0; I < Len; I += 4) {
    const int FinalBlock = I + 4 == Len;
    if (b64DecodeChar(S[I]) < 0 || b64DecodeChar(S[I + 1]) < 0 ||
        (S[I + 2] != '=' && b64DecodeChar(S[I + 2]) < 0) ||
        (S[I + 3] != '=' && b64DecodeChar(S[I + 3]) < 0) ||
        (!FinalBlock && (S[I + 2] == '=' || S[I + 3] == '=')) ||
        (S[I + 2] == '=' && S[I + 3] != '=')) {
      char *Empty = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 1);
      Empty[0] = '\0';
      return Empty;
    }
  }
  size_t OutLen = 3 * Len / 4;
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, OutLen + 1);
  size_t J = 0;
  for (size_t I = 0; I < Len; I += 4) {
    int A = b64DecodeChar(S[I]);
    int B = (I + 1 < Len) ? b64DecodeChar(S[I + 1]) : 0;
    int C = S[I + 2] == '=' ? 0 : b64DecodeChar(S[I + 2]);
    int D = S[I + 3] == '=' ? 0 : b64DecodeChar(S[I + 3]);
    uint32_t Triple = ((uint32_t)A << 18) | ((uint32_t)B << 12) |
                      ((uint32_t)C << 6) | (uint32_t)D;
    R[J++] = (Triple >> 16) & 0xFF;
    if (I + 2 < Len && S[I + 2] != '=')
      R[J++] = (Triple >> 8) & 0xFF;
    if (I + 3 < Len && S[I + 3] != '=')
      R[J++] = Triple & 0xFF;
  }
  R[J] = '\0';
  return R;
}

static const char HexChars[] = "0123456789abcdef";

const char *YonaStdEncodingHexEncode(const char *S) {
  size_t Len = strlen(S);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len * 2 + 1);
  for (size_t I = 0; I < Len; I++) {
    R[I * 2] = HexChars[((uint8_t)S[I] >> 4) & 0xF];
    R[I * 2 + 1] = HexChars[(uint8_t)S[I] & 0xF];
  }
  R[Len * 2] = '\0';
  return R;
}

static int hexVal(char C) {
  if (C >= '0' && C <= '9')
    return C - '0';
  if (C >= 'a' && C <= 'f')
    return C - 'a' + 10;
  if (C >= 'A' && C <= 'F')
    return C - 'A' + 10;
  return 0;
}

const char *YonaStdEncodingHexDecode(const char *S) {
  size_t Len = strlen(S);
  size_t OutLen = Len / 2;
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, OutLen + 1);
  for (size_t I = 0; I < OutLen; I++)
    R[I] = (char)((hexVal(S[I * 2]) << 4) | hexVal(S[I * 2 + 1]));
  R[OutLen] = '\0';
  return R;
}

const char *YonaStdEncodingUrlEncode(const char *S) {
  size_t Len = strlen(S);
  /* Worst case: every char is encoded as %XX (3x) */
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len * 3 + 1);
  size_t J = 0;
  for (size_t I = 0; I < Len; I++) {
    unsigned char C = (unsigned char)S[I];
    if (isalnum(C) || C == '-' || C == '_' || C == '.' || C == '~') {
      R[J++] = C;
    } else {
      R[J++] = '%';
      R[J++] = HexChars[(C >> 4) & 0xF];
      R[J++] = HexChars[C & 0xF];
    }
  }
  R[J] = '\0';
  return R;
}

const char *YonaStdEncodingUrlDecode(const char *S) {
  size_t Len = strlen(S);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  size_t J = 0;
  for (size_t I = 0; I < Len; I++) {
    if (S[I] == '%' && I + 2 < Len) {
      R[J++] = (char)((hexVal(S[I + 1]) << 4) | hexVal(S[I + 2]));
      I += 2;
    } else if (S[I] == '+') {
      R[J++] = ' ';
    } else {
      R[J++] = S[I];
    }
  }
  R[J] = '\0';
  return R;
}

const char *YonaStdEncodingHtmlEscape(const char *S) {
  size_t Len = strlen(S);
  /* Worst case: every char becomes &amp; (5x) */
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len * 6 + 1);
  size_t J = 0;
  for (size_t I = 0; I < Len; I++) {
    switch (S[I]) {
    case '&':
      memcpy(R + J, "&amp;", 5);
      J += 5;
      break;
    case '<':
      memcpy(R + J, "&lt;", 4);
      J += 4;
      break;
    case '>':
      memcpy(R + J, "&gt;", 4);
      J += 4;
      break;
    case '"':
      memcpy(R + J, "&quot;", 6);
      J += 6;
      break;
    case '\'':
      memcpy(R + J, "&#39;", 5);
      J += 5;
      break;
    default:
      R[J++] = S[I];
      break;
    }
  }
  R[J] = '\0';
  return R;
}

/* ===== Platform I/O wrappers ===== */
/* Platform-specific implementations are in src/Runtime/Platform/ (C sources)
 *
 * with shared headers in include/yona/Runtime/. See CMakeLists.txt. */
#include "yona/Runtime/Platform/Api.h"

/* YonaRuntimeIoAwait: platform/file_linux.c (io_uring) on Linux;
 * platform/file_macos.c (kqueue) on macOS;
 * platform/file_windows.c (direct-result table) on Windows. */

/* Resource cleanup for `with` expression. Closes file descriptors,
 * sockets, or other resources based on the value type. */
void YonaRuntimeClose(int64_t Handle) {
  /* Only close valid-looking file descriptors (small positive integers).
   * Arbitrary i64 values (pointers, large ints) are not fds. */
  if (Handle > 2 && Handle < 65536)
    YonaRuntimePlatformCloseFileHandle((int)Handle);
}

/* Std\Io — non-blocking writes via io_uring, non-blocking line reads
 * via the thread pool. Every function that can block submits its work
 * off the calling task and returns a uring / async id; the codegen's
 * IO / AFN markers auto-await at the use site.
 *
 * The int64_t fd argument unpacks directly from the FileHandle ADT
 * wrapper (heap-boxed `{tag, fd}`); user code writes
 * `case h of FileHandle fd -> ...` to get the raw fd, and the stdin /
 * stdout / stderr CAFs in Std\Io return already-extracted fd ints. */

extern int64_t
YonaRuntimePlatformSubmitFileDescriptorStringWrite(int Fd, const char *S);
extern int64_t
YonaRuntimePlatformSubmitFileDescriptorStringsWrite(int Fd, const char *S1,
                                                    const char *S2);
extern const char *YonaRuntimePlatformReadLineFromFileDescriptor(int Fd);

/* Write a string to an fd (no trailing newline). Returns uring ID. */
int64_t YonaStdIoWriteStr(int64_t Fd, const char *S) {
  return YonaRuntimePlatformSubmitFileDescriptorStringWrite((int)Fd, S);
}

/* Write a string followed by "\n". Returns uring ID. */
int64_t YonaStdIoWriteLine(int64_t Fd, const char *S) {
  return YonaRuntimePlatformSubmitFileDescriptorStringsWrite((int)Fd, S, "\n");
}

int64_t YonaStdIoPrint(const char *S) { return YonaStdIoWriteStr(1, S); }

int64_t YonaStdIoPrintln(const char *S) { return YonaStdIoWriteLine(1, S); }

int64_t YonaStdIoEprint(const char *S) { return YonaStdIoWriteStr(2, S); }

int64_t YonaStdIoEprintln(const char *S) { return YonaStdIoWriteLine(2, S); }

int64_t YonaStdIoPutStr(int64_t Fd, const char *S) {
  return YonaStdIoWriteStr(Fd, S);
}

int64_t YonaStdIoPutStrLn(int64_t Fd, const char *S) {
  return YonaStdIoWriteLine(Fd, S);
}

int64_t YonaStdIoWrite(int64_t Fd, const char *S) {
  return YonaStdIoWriteStr(Fd, S);
}

/* Thread-pool async: read one line from fd. Returns Option String
 * (heap-allocated Some line / None at EOF). Called via AFN so the
 * read blocks on a worker thread, not the calling task. */
int64_t YonaStdIoReadLineFd(int64_t Fd) {
  const char *Line = YonaRuntimePlatformReadLineFromFileDescriptor((int)Fd);
  if (!Line) {
    /* EOF → None */
    int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
        YONA_RUNTIME_TYPE_ADT,
        (YONA_STDLIB_ADT_HEADER_SIZE + 0) * sizeof(int64_t));
    Adt[0] = 1; /* tag = None */
    Adt[1] = 0; /* num_fields */
    Adt[2] = 0; /* heap_mask */
    return (int64_t)(intptr_t)Adt;
  }
  /* Some line — the string is heap-owned, so bit 0 of heap_mask is set. */
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_ADT,
      (YONA_STDLIB_ADT_HEADER_SIZE + 1) * sizeof(int64_t));
  Adt[0] = 0; /* tag = Some */
  Adt[1] = 1; /* num_fields */
  Adt[2] = 1; /* heap_mask — field 0 is heap */
  Adt[3] = (int64_t)(intptr_t)Line;
  return (int64_t)(intptr_t)Adt;
}

/* Synchronous: isatty(fd). Cheap syscall, no async needed. */
int64_t YonaStdIoIsTty(int64_t Fd) { return isatty((int)Fd) == 1 ? 1 : 0; }

/* Synchronous: fsync(fd). Most io_uring writes are durable at
 * completion; this is a backstop for user-managed buffers. */
int64_t YonaStdIoFlushFd(int64_t Fd) {
  return YonaRuntimePlatformFlushFileHandle((int)Fd);
}

/* Std\File — async ops return uring ID, sync ops return directly */
/* Register a direct result for io_await fallback when io_uring is unavailable
 */
extern int64_t YonaRuntimeIoRegisterDirectResult(void *Result);
#define YONA_IO_REGISTER_DIRECT_RESULT YonaRuntimeIoRegisterDirectResult

int64_t YonaStdFileReadFile(const char *Path) {
  int64_t Id = YonaRuntimePlatformSubmitFileRead(Path);
  if (Id > 0)
    return Id; /* uring ID = promise */
  /* io_uring unavailable: read synchronously, wrap result for io_await */
  char *Data = YonaRuntimePlatformReadFile(Path);
  return YONA_IO_REGISTER_DIRECT_RESULT(Data);
}
int64_t YonaStdFileWriteFile(const char *Path, const char *Content) {
  int64_t Id = YonaRuntimePlatformSubmitFileWrite(Path, Content);
  if (Id > 0)
    return Id; /* uring ID = promise */
  int64_t Ok = YonaRuntimePlatformWriteFile(Path, Content) == 0 ? 1 : 0;
  return YONA_IO_REGISTER_DIRECT_RESULT((void *)(intptr_t)Ok);
}
int64_t YonaStdFileAppendFile(const char *Path, const char *Content) {
  return YonaRuntimePlatformAppendFile(Path, Content) == 0 ? 1 : 0;
}
int64_t YonaStdFileExists(const char *Path) {
  return YonaRuntimePlatformFileExists(Path);
}
int64_t YonaStdFileRemove(const char *Path) {
  return YonaRuntimePlatformRemoveFile(Path) == 0 ? 1 : 0;
}
int64_t YonaStdFileSize(const char *Path) {
  return YonaRuntimePlatformFileSize(Path);
}
int64_t *YonaStdFileListDir(const char *Path) {
  return YonaRuntimePlatformListDirectory(Path);
}

/* readLines: read file, split by newlines, return seq of strings */
/* readLines now returns an Iterator String (streaming, O(64KB) memory).
 * The Iterator wraps a 64KB-buffered file reader that yields lines on demand.
 */
int64_t YonaStdFileReadLines(const char *Path) {
  return YonaRuntimeFileLineIteratorCreate(Path);
}

/* ===== Std\Set — persistent hash set (HAMT-backed) ===== */
int64_t *YonaStdSetInsert(int64_t *Set, int64_t Elem) {
  return YonaRuntimeSetInsert(Set, Elem);
}
int64_t YonaStdSetContains(int64_t *Set, int64_t Elem) {
  return YonaRuntimeSetContains(Set, Elem);
}
int64_t YonaStdSetSize(int64_t *Set) { return YonaRuntimeSetSize(Set); }
int64_t *YonaStdSetElements(int64_t *Set) {
  return YonaRuntimeSetElements(Set);
}
int64_t *YonaStdSetUnion(int64_t *A, int64_t *B) {
  return YonaRuntimeSetUnion(A, B);
}
int64_t *YonaStdSetIntersection(int64_t *A, int64_t *B) {
  return YonaRuntimeSetIntersection(A, B);
}
int64_t *YonaStdSetDifference(int64_t *A, int64_t *B) {
  return YonaRuntimeSetDifference(A, B);
}

/* ===== Std\Dict — persistent hash map (HAMT) ===== */
int64_t *YonaStdDictPut(int64_t *Dict, int64_t Key, int64_t Value) {
  return YonaRuntimeDictionaryPut(Dict, Key, Value);
}
int64_t YonaStdDictGet(int64_t *Dict, int64_t Key, int64_t DefaultVal) {
  return YonaRuntimeDictionaryGet(Dict, Key, DefaultVal);
}
int64_t YonaStdDictContains(int64_t *Dict, int64_t Key) {
  return YonaRuntimeDictionaryContains(Dict, Key);
}
int64_t YonaStdDictSize(int64_t *Dict) {
  return YonaRuntimeDictionarySize(Dict);
}
int64_t *YonaStdDictKeys(int64_t *Dict) {
  return YonaRuntimeDictionaryKeys(Dict);
}

/* Binary file I/O */
int64_t YonaStdFileReadFileBytes(const char *Path) {
  extern int64_t YonaRuntimePlatformSubmitFileByteRead(const char *Path);
  int64_t Id = YonaRuntimePlatformSubmitFileByteRead(Path);
  if (Id > 0)
    return Id;
  void *Bytes =
      YonaRuntimeByteArrayFromString(YonaRuntimePlatformReadFile(Path));
  return YONA_IO_REGISTER_DIRECT_RESULT(Bytes);
}

int64_t YonaStdFileWriteFileBytes(const char *Path, void *Bytes) {
  int64_t *B = (int64_t *)Bytes;
  int64_t Len = B[0];
  uint8_t *Data = (uint8_t *)(B + 1);
  /* Sync write for binary data */
  FILE *F = fopen(Path, "wb");
  if (!F)
    return 0;
  size_t W = fwrite(Data, 1, (size_t)Len, F);
  fclose(F);
  return (W == (size_t)Len) ? 1 : 0;
}

/* ===== Std\File — handle-based binary I/O ===== */

/* Helper: extract fd from FileHandle or its owning Linear wrapper.
 * Linear is an ADT with one heap field, so its field 0 points at FileHandle. */
static int fhFd(int64_t HandleI64) {
  int64_t *Handle = (int64_t *)(intptr_t)HandleI64;
  if (Handle && Handle[1] == 1 && (Handle[2] & 1) != 0)
    Handle = (int64_t *)(intptr_t)Handle[3];
  return (int)Handle[3]; /* ADT_HDR_SIZE=3, field 0 = fd */
}

/* openFile: open file and return Linear(FileHandle) wrapping the fd.
 * mode is a FileMode ADT: Read=0, Write=1, ReadWrite=2, Append=3.
 * Returns FileHandle ADT: [tag=0, num_fields=1, heap_mask=0, fd] */
int64_t YonaStdFileOpenFile(const char *Path, int64_t ModeI64) {
  int64_t ModeTag = ModeI64;
  if (ModeI64 > 16) {
    int64_t *ModeAdt = (int64_t *)(intptr_t)ModeI64;
    ModeTag = ModeAdt[0]; /* recursive ADT layout: [tag, num_fields,
                               heap_mask, ...] */
  }
  int Fd = (int)YonaRuntimePlatformOpenFileHandle(Path, ModeTag);
  if (Fd < 0) {
    YonaRuntimeRaise(0, "cannot open file");
    return 0;
  }

  /* Wrap in FileHandle ADT: [tag=0, num_fields=1, heap_mask=0, fd] */
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_ADT,
                                                4 * sizeof(int64_t));
  Adt[0] = 0; /* tag = FileHandle */
  Adt[1] = 1; /* num_fields */
  Adt[2] = 0; /* heap_mask = 0 (fd is not heap) */
  Adt[3] = (int64_t)Fd;
  /* Linear owns the FileHandle pointer through its heap mask. */
  int64_t *Linear = (int64_t *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_ADT,
                                                   4 * sizeof(int64_t));
  Linear[0] = 0; /* tag = Linear */
  Linear[1] = 1;
  Linear[2] = 1; /* field 0 is heap-owned */
  Linear[3] = (int64_t)(intptr_t)Adt;
  return (int64_t)(intptr_t)Linear;
}

/* closeFileHandle: extract fd from FileHandle ADT and close it */
int64_t YonaStdFileCloseFileHandle(int64_t HandleI64) {
  int Fd = fhFd(HandleI64);
  YonaRuntimePlatformCloseFileHandle(Fd);
  return 0;
}

/* readBytes: read up to count bytes from FileHandle at current position.
 * Uses io_uring pread with userspace position tracking. */
int64_t YonaStdFileReadBytes(int64_t Handle, int64_t Count) {
  int Fd = fhFd(Handle);
  int64_t Pos = YonaRuntimePlatformTellFileHandle(Fd);
  if (Pos < 0)
    Pos = 0;
  extern int64_t YonaRuntimePlatformSubmitFileDescriptorByteRead(
      int Fd, int64_t Count, int64_t Offset);
  int64_t Id = YonaRuntimePlatformSubmitFileDescriptorByteRead(Fd, Count, Pos);
  YonaRuntimePlatformSeekFileHandle(Fd, Pos + Count, 0);
  return Id;
}

/* writeBytes: write Bytes to FileHandle via io_uring pwrite. Returns bytes
 * written. */
int64_t YonaStdFileWriteBytes(int64_t Handle, int64_t BytesI64) {
  int Fd = fhFd(Handle);
  void *Bytes = (void *)(intptr_t)BytesI64;
  int64_t *B = (int64_t *)Bytes;
  int64_t Len = B[0];
  int64_t Pos = YonaRuntimePlatformTellFileHandle(Fd);
  if (Pos < 0)
    Pos = 0;
  extern int64_t YonaRuntimePlatformSubmitFileDescriptorByteWrite(
      int Fd, void *Bytes, int64_t Offset);
  int64_t Id = YonaRuntimePlatformSubmitFileDescriptorByteWrite(Fd, Bytes, Pos);
  YonaRuntimePlatformSeekFileHandle(Fd, Pos + Len, 0);
  return Id;
}

/* seek: set file position. whence is a Whence ADT: SeekSet=0, SeekCur=1,
 * SeekEnd=2 */
int64_t YonaStdFileSeek(int64_t Handle, int64_t Offset, int64_t WhenceI64) {
  int Fd = fhFd(Handle);
  int64_t WhenceTag = WhenceI64;
  if (WhenceI64 > 16) {
    int64_t *WhenceAdt = (int64_t *)(intptr_t)WhenceI64;
    WhenceTag = WhenceAdt[0];
  }
  return YonaRuntimePlatformSeekFileHandle(Fd, Offset, WhenceTag);
}

/* tell: get current file position */
int64_t YonaStdFileTell(int64_t Handle) {
  int Fd = fhFd(Handle);
  return YonaRuntimePlatformTellFileHandle(Fd);
}

/* flush: fsync */
int64_t YonaStdFileFlush(int64_t Handle) {
  int Fd = fhFd(Handle);
  return YonaRuntimePlatformFlushFileHandle(Fd);
}

/* truncate: ftruncate */
int64_t YonaStdFileTruncate(int64_t Handle, int64_t Length) {
  int Fd = fhFd(Handle);
  return YonaRuntimePlatformTruncateFileHandle(Fd, Length);
}

/* readChunks: create streaming binary chunk iterator from FileHandle */
int64_t YonaStdFileReadChunks(int64_t Handle, int64_t ChunkSize) {
  int Fd = fhFd(Handle);
  return YonaRuntimeFileChunkIteratorCreate((int64_t)Fd, ChunkSize);
}

/* Std\Process */
const char *YonaStdProcessGetenv(const char *Name) {
  return YonaRuntimePlatformGetEnvironment(Name);
}
const char *YonaStdProcessGetcwd(void) {
  return YonaRuntimePlatformGetCurrentWorkingDirectory();
}
int64_t YonaStdProcessExit(int64_t Code) {
  return YonaRuntimePlatformExitProcess(Code);
}
const char *YonaStdProcessExec(const char *Executable,
                               int64_t *ArgumentSequence) {
  return YonaRuntimePlatformExecute(Executable, ArgumentSequence);
}
int64_t YonaStdProcessExecStatus(const char *Executable,
                                 int64_t *ArgumentSequence) {
  return YonaRuntimePlatformExecuteStatus(Executable, ArgumentSequence);
}
int64_t YonaStdProcessSetenv(const char *Name, const char *Value) {
  return YonaRuntimePlatformSetEnvironment(Name, Value);
}
const char *YonaStdProcessHostname(void) {
  return YonaRuntimePlatformHostName();
}

static int RuntimeArgumentCount = 0;
static char **RuntimeArgumentValues = NULL;

void YonaRuntimeProcessSetArguments(int Argc, char **Argv) {
  RuntimeArgumentCount = Argc;
  RuntimeArgumentValues = Argv;
}

static char *YonaRuntimeCopyCString(const char *Src) {
  if (!Src)
    Src = "";
  size_t N = strlen(Src);
  char *R = (char *)YonaRuntimeAllocateString(N + 1);
  memcpy(R, Src, N + 1);
  return R;
}

int64_t *YonaStdProcessGetArgs(void) {
  int64_t N = RuntimeArgumentCount > 0 ? (int64_t)RuntimeArgumentCount : 0;
  int64_t *Seq = YonaRuntimeSequenceAllocate(N);
  for (int64_t I = 0; I < N; I++) {
    const char *A = (RuntimeArgumentValues && RuntimeArgumentValues[I])
                        ? RuntimeArgumentValues[I]
                        : "";
    YonaRuntimeSequenceSet(Seq, I,
                           (int64_t)(intptr_t)YonaRuntimeCopyCString(A));
  }
  YonaRuntimeSequenceSetHeap(Seq, 1);
  return Seq;
}

const char *YonaStdProcessYonaVersion(void) {
  return YonaRuntimeCopyCString(YONA_VERSION_STRING);
}

char *YonaStdIoReadStdinImpl(int64_t Unit) {
  (void)Unit;
  const size_t MaxCap = 64u * 1024u * 1024u;
  size_t Cap = 4096, Len = 0;
  char *Buf = (char *)malloc(Cap);
  if (!Buf)
    return YonaRuntimeCopyCString("");
  for (;;) {
    if (Len >= Cap) {
      if (Cap >= MaxCap)
        break;
      size_t Ncap = Cap * 2;
      if (Ncap > MaxCap)
        Ncap = MaxCap;
      char *Nb = (char *)realloc(Buf, Ncap);
      if (!Nb)
        break;
      Buf = Nb;
      Cap = Ncap;
    }
#if defined(_WIN32)
    int N = (int)read(0, Buf + Len, (unsigned)(Cap - Len));
#else
    ssize_t n = read(0, buf + len, cap - len);
#endif
    if (N <= 0)
      break;
    Len += (size_t)N;
  }
  char *R = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(R, Buf, Len);
  R[Len] = '\0';
  free(Buf);
  return R;
}

/* readExact: stream read() loop — pipe/socket safe (not pread/seek).
 * `fd_or_handle` is a raw descriptor (stdin is 0) or a FileHandle ADT. */
#ifndef YONA_READ_EXACT_MAX
#define YONA_READ_EXACT_MAX (16u * 1024u * 1024u)
#endif

#if defined(_WIN32)
/* Content-Length framing is byte-exact. CRT stdin/stdout default to text
 * mode, so _read/_write translate CRLF and desync LSP headers (C++ yls
 * already calls _setmode on cin/cout). */
static void setWindowsStandardIoBinary(int64_t FdOrHandle) {
  if (FdOrHandle >= 0 && FdOrHandle <= 2)
    (void)_setmode((int)FdOrHandle, _O_BINARY);
}
#endif

char *YonaStdIoReadExactBytes(int64_t FdOrHandle, int64_t N) {
#if defined(_WIN32)
  setWindowsStandardIoBinary(FdOrHandle);
#endif
  if (N <= 0 || (uint64_t)N > YONA_READ_EXACT_MAX) {
    char *R = (char *)YonaRuntimeAllocateStringWithLength(1, 0);
    R[0] = '\0';
    return R;
  }
  int Fd;
  if (FdOrHandle > 65536) {
    int64_t *Handle = (int64_t *)(intptr_t)FdOrHandle;
    /* Linear FileHandle is [tag, 1, heap, inner]; FileHandle is [tag, 1, 0,
     * fd]. */
    if (Handle[1] == 1 && Handle[2] != 0 && Handle[3] > 65536) {
      int64_t *Inner = (int64_t *)(intptr_t)Handle[3];
      Fd = (int)Inner[3];
    } else {
      Fd = (int)Handle[3];
    }
  } else {
    Fd = (int)FdOrHandle;
  }
  size_t Want = (size_t)N;
  char *Buf = (char *)malloc(Want);
  if (!Buf) {
    char *R = (char *)YonaRuntimeAllocateStringWithLength(1, 0);
    R[0] = '\0';
    return R;
  }
  size_t Got = 0;
  while (Got < Want) {
#if defined(_WIN32)
    int K = (int)_read(Fd, Buf + Got, (unsigned)(Want - Got));
#else
    ssize_t k = read(fd, buf + got, want - got);
#endif
    if (K <= 0)
      break;
    Got += (size_t)K;
  }
  char *R = (char *)YonaRuntimeAllocateStringWithLength(Got + 1, Got);
  memcpy(R, Buf, Got);
  R[Got] = '\0';
  free(Buf);
  return R;
}

/* Result Json/String-style ADT: Ok tag 0 | Err tag 1, one heap field. */
static int64_t ioResultOkString(char *S) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_ADT,
      (YONA_STDLIB_ADT_HEADER_SIZE + 1) * sizeof(int64_t));
  Adt[0] = 0;
  Adt[1] = 1;
  Adt[2] = 1;
  Adt[3] = (int64_t)(intptr_t)S;
  return (int64_t)(intptr_t)Adt;
}

static int64_t ioResultError(const char *Msg) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_ADT,
      (YONA_STDLIB_ADT_HEADER_SIZE + 1) * sizeof(int64_t));
  Adt[0] = 1;
  Adt[1] = 1;
  Adt[2] = 1;
  Adt[3] = (int64_t)(intptr_t)YonaRuntimeCopyCString(Msg);
  return (int64_t)(intptr_t)Adt;
}

int64_t YonaStdIoReadExact(int64_t FdOrHandle, int64_t N) {
  if (N < 0)
    return ioResultError("negative count");
  if ((uint64_t)N > YONA_READ_EXACT_MAX)
    return ioResultError("too large");
  char *S = YonaStdIoReadExactBytes(FdOrHandle, N);
  if (YonaRuntimeStringLength(S) == N)
    return ioResultOkString(S);
  return ioResultError("unexpected eof");
}

/* Synchronous write — no Promise. Safe for LSP Content-Length framing. */
void YonaStdIoWriteBytes(int64_t FdOrHandle, const char *S) {
  if (!S)
    return;
#if defined(_WIN32)
  setWindowsStandardIoBinary(FdOrHandle);
#endif
  int Fd;
  if (FdOrHandle > 65536) {
    int64_t *Handle = (int64_t *)(intptr_t)FdOrHandle;
    if (Handle[1] == 1 && Handle[2] != 0 && Handle[3] > 65536) {
      int64_t *Inner = (int64_t *)(intptr_t)Handle[3];
      Fd = (int)Inner[3];
    } else {
      Fd = (int)Handle[3];
    }
  } else {
    Fd = (int)FdOrHandle;
  }
  size_t N = (size_t)YonaRuntimeStringLength(S);
  size_t Off = 0;
  while (Off < N) {
#if defined(_WIN32)
    int K = (int)_write(Fd, S + Off, (unsigned)(N - Off));
#else
    ssize_t k = write(fd, s + off, n - off);
#endif
    if (K <= 0)
      break;
    Off += (size_t)K;
  }
}

/* ===== Std\Http — HTTP client ===== */

/* Build an HTTP/1.1 request string */
const char *YonaStdHttpBuildRequest(const char *Method, const char *Host,
                                    const char *Path, const char *Body) {
  size_t BodyLen = Body ? strlen(Body) : 0;
  size_t HostLen = strlen(Host);
  size_t PathLen = strlen(Path);
  size_t MethodLen = strlen(Method);

  /* Calculate total size */
  size_t Total = MethodLen + 1 + PathLen + 11 /* " HTTP/1.1\r\n" */
                 + 6 + HostLen + 2            /* "Host: ...\r\n" */
                 + 24                         /* "Connection: close\r\n" */
                 + 19 /* "User-Agent: Yona\r\n" */;
  if (BodyLen > 0) {
    Total += 32   /* "Content-Length: NNN\r\n" */
             + 48 /* "Content-Type: application/x-www-form-urlencoded\r\n" */
             + 2 + BodyLen;
  } else {
    Total += 2; /* final \r\n */
  }

  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Total + 64);
  int N;
  if (BodyLen > 0) {
    N = snprintf(R, Total + 64,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: Yona\r\n"
                 "Connection: close\r\n"
                 "Content-Length: %zu\r\n"
                 "Content-Type: application/x-www-form-urlencoded\r\n"
                 "\r\n"
                 "%s",
                 Method, Path, Host, BodyLen, Body);
  } else {
    N = snprintf(R, Total + 64,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: Yona\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 Method, Path, Host);
  }
  R[N] = '\0';
  return R;
}

/* Parse HTTP status code from response string */
int64_t YonaStdHttpParseStatus(const char *Response) {
  /* HTTP/1.1 200 OK\r\n... */
  if (strncmp(Response, "HTTP/", 5) != 0)
    return 0;
  const char *Sp = strchr(Response, ' ');
  if (!Sp)
    return 0;
  return (int64_t)atoi(Sp + 1);
}

/* Extract HTTP response body (after \r\n\r\n) */
const char *YonaStdHttpParseBody(const char *Response) {
  const char *Body = strstr(Response, "\r\n\r\n");
  if (!Body) {
    char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 1);
    R[0] = '\0';
    return R;
  }
  Body += 4;
  size_t Len = strlen(Body);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  memcpy(R, Body, Len + 1);
  return R;
}

/* Extract a specific HTTP header value */
const char *YonaStdHttpGetHeader(const char *Name, const char *Response) {
  size_t NameLen = strlen(Name);
  const char *P = Response;
  while ((P = strstr(P, Name)) != NULL) {
    /* Check it's at start of line */
    if (P == Response || *(P - 1) == '\n') {
      P += NameLen;
      if (*P == ':') {
        P++;
        while (*P == ' ')
          P++;
        const char *End = strstr(P, "\r\n");
        size_t Len = End ? (size_t)(End - P) : strlen(P);
        char *R =
            (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
        memcpy(R, P, Len);
        R[Len] = '\0';
        return R;
      }
    }
    P++;
  }
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 1);
  R[0] = '\0';
  return R;
}

/* Parse URL into a mixed-field ADT payload: (host, port, path).
 * A Seq cannot represent this safely: its single heap flag applies to every
 * element, while only host and path are reference-counted strings. */
int64_t *YonaStdHttpParseUrl(const char *Url) {
  int64_t *Result = (int64_t *)YonaRuntimeAdtAllocate(0, 3);
  int Port = 80;
  const char *HostStart = Url;
  const char *PathStart = "/";

  /* Skip scheme */
  if (strncmp(Url, "http://", 7) == 0) {
    HostStart = Url + 7;
    Port = 80;
  } else if (strncmp(Url, "https://", 8) == 0) {
    HostStart = Url + 8;
    Port = 443;
  }

  /* Find path */
  const char *Slash = strchr(HostStart, '/');
  size_t HostLen;
  if (Slash) {
    HostLen = (size_t)(Slash - HostStart);
    PathStart = Slash;
  } else {
    HostLen = strlen(HostStart);
  }

  /* Check for port in host */
  const char *Colon = (const char *)memchr(HostStart, ':', HostLen);
  if (Colon) {
    Port = atoi(Colon + 1);
    HostLen = (size_t)(Colon - HostStart);
  }

  char *Host =
      (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, HostLen + 1);
  memcpy(Host, HostStart, HostLen);
  Host[HostLen] = '\0';

  size_t PathLen = strlen(PathStart);
  char *Path =
      (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, PathLen + 1);
  memcpy(Path, PathStart, PathLen + 1);

  YonaRuntimeAdtSetField(Result, 0, (int64_t)(intptr_t)Host);
  YonaRuntimeAdtSetField(Result, 1, (int64_t)Port);
  YonaRuntimeAdtSetField(Result, 2, (int64_t)(intptr_t)Path);
  YonaRuntimeAdtSetHeapMask(Result, 5); /* host and path are heap values */
  return Result;
}

/* YonaStdHttpHttpGet: platform/net_linux.c (io_uring) or net_windows.c
 * (Winsock). */

/* Std\Random — pseudo-random number generation */

static int YonaRandomInitialized = 0;

static void yonaRandomInit(void) {
  if (!YonaRandomInitialized) {
    srand((unsigned)time(NULL));
    YonaRandomInitialized = 1;
  }
}

int64_t YonaStdRandomInt(int64_t Lo, int64_t Hi) {
  yonaRandomInit();
  if (Lo >= Hi)
    return Lo;
  return Lo + (int64_t)(rand() % (Hi - Lo + 1));
}

double YonaStdRandomFloat(void) {
  yonaRandomInit();
  return (double)rand() / (double)RAND_MAX;
}

int64_t YonaStdRandomChoice(int64_t *Seq) {
  yonaRandomInit();
  int64_t Len = Seq[0];
  if (Len <= 0)
    return 0;
  return YonaRuntimeSequenceGet(Seq, (int64_t)(rand() % Len));
}

int64_t *YonaStdRandomShuffle(int64_t *Seq) {
  yonaRandomInit();
  int64_t Len = Seq[0];
  int64_t *Result = YonaRuntimeSequenceAllocate(Len);
  for (int64_t I = 0; I < Len; I++)
    YonaRuntimeSequenceSet(Result, I, YonaRuntimeSequenceGet(Seq, I));
  /* Fisher-Yates shuffle */
  for (int64_t I = Len - 1; I > 0; I--) {
    int64_t J = (int64_t)(rand() % (I + 1));
    int64_t Tmp = YonaRuntimeSequenceGet(Result, I);
    YonaRuntimeSequenceSet(Result, I, YonaRuntimeSequenceGet(Result, J));
    YonaRuntimeSequenceSet(Result, J, Tmp);
  }
  return Result;
}

/* ===== Std\Time — time measurement and utilities ===== */

#if defined(__linux__) || defined(__APPLE__)
#include <sys/time.h>
#endif

/* 100-ns intervals from 1601-01-01 to 1970-01-01 */
#if defined(_WIN32)
#define YONA_WIN_EPOCH_100NS 116444736000000000ULL
#endif

/* Current time as epoch milliseconds */
int64_t YonaStdTimeNow(void) {
#if defined(_WIN32)
  FILETIME Ft;
  ULARGE_INTEGER U;
  GetSystemTimeAsFileTime(&Ft);
  U.LowPart = Ft.dwLowDateTime;
  U.HighPart = Ft.dwHighDateTime;
  return (int64_t)((U.QuadPart - YONA_WIN_EPOCH_100NS) / 10000ULL);
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
#endif
}

/* Current time as epoch microseconds */
int64_t YonaStdTimeNowMicros(void) {
#if defined(_WIN32)
  FILETIME Ft;
  ULARGE_INTEGER U;
  GetSystemTimeAsFileTime(&Ft);
  U.LowPart = Ft.dwLowDateTime;
  U.HighPart = Ft.dwHighDateTime;
  return (int64_t)((U.QuadPart - YONA_WIN_EPOCH_100NS) / 10ULL);
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000 + (int64_t)tv.tv_usec;
#endif
}

/* Current time as epoch seconds */
int64_t YonaStdTimeEpoch(void) { return (int64_t)time(NULL); }

/* Sleep for N milliseconds */
void YonaStdTimeSleep(int64_t Ms) {
#if defined(_WIN32)
  if (Ms <= 0)
    return;
  while (Ms > 0) {
    DWORD Chunk = (Ms > 0x7FFFFFFF) ? 0x7FFFFFFF : (DWORD)Ms;
    Sleep(Chunk);
    Ms -= (int64_t)Chunk;
  }
#else
  usleep((useconds_t)(ms * 1000));
#endif
}

/* Format epoch seconds as ISO 8601 string (YYYY-MM-DD HH:MM:SS) */
const char *YonaStdTimeFormat(int64_t EpochSecs) {
  time_t T = (time_t)EpochSecs;
  struct tm *Tm = localtime(&T);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 20);
  strftime(R, 20, "%Y-%m-%d %H:%M:%S", Tm);
  return R;
}

/* Elapsed milliseconds between two now() timestamps */
int64_t YonaStdTimeElapsed(int64_t Start, int64_t End) { return End - Start; }

/* ===== Std\Path — file path manipulation ===== */

static int yonaPathIsSep(char C) {
#ifdef _WIN32
  return C == '/' || C == '\\';
#else
  return c == '/';
#endif
}

const char *YonaStdPathJoin(const char *A, const char *B) {
  size_t La = strlen(A), Lb = strlen(B);
  /* Skip trailing slash on a */
  if (La > 0 && yonaPathIsSep(A[La - 1]))
    La--;
  /* Skip leading slash on b */
  const char *Bs = B;
  if (Lb > 0 && yonaPathIsSep(B[0])) {
    Bs++;
    Lb--;
  }
  char *R =
      (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, La + 1 + Lb + 1);
  memcpy(R, A, La);
  R[La] = '/';
  memcpy(R + La + 1, Bs, Lb);
  R[La + 1 + Lb] = '\0';
  return R;
}

const char *YonaStdPathDirname(const char *Path) {
  size_t Len = strlen(Path);
  /* Find last slash */
  const char *Last = NULL;
  for (size_t I = 0; I < Len; I++)
    if (yonaPathIsSep(Path[I]))
      Last = Path + I;
  if (!Last) {
    char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 2);
    R[0] = '.';
    R[1] = '\0';
    return R;
  }
  size_t Dlen = (size_t)(Last - Path);
  if (Dlen == 0)
    Dlen = 1; /* root "/" */
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Dlen + 1);
  memcpy(R, Path, Dlen);
  R[Dlen] = '\0';
  return R;
}

const char *YonaStdPathBasename(const char *Path) {
  size_t Len = strlen(Path);
  const char *Last = Path;
  for (size_t I = 0; I < Len; I++)
    if (yonaPathIsSep(Path[I]))
      Last = Path + I + 1;
  size_t Blen = strlen(Last);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Blen + 1);
  memcpy(R, Last, Blen + 1);
  return R;
}

const char *YonaStdPathExtension(const char *Path) {
  const char *Base = Path;
  size_t Len = strlen(Path);
  for (size_t I = 0; I < Len; I++)
    if (yonaPathIsSep(Path[I]))
      Base = Path + I + 1;
  const char *Dot = NULL;
  for (const char *P = Base; *P; P++)
    if (*P == '.')
      Dot = P;
  if (!Dot || Dot == Base) {
    char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 1);
    R[0] = '\0';
    return R;
  }
  size_t Elen = strlen(Dot);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Elen + 1);
  memcpy(R, Dot, Elen + 1);
  return R;
}

const char *YonaStdPathWithExtension(const char *Path, const char *Ext) {
  /* Find last dot in basename */
  const char *Base = Path;
  size_t Len = strlen(Path);
  for (size_t I = 0; I < Len; I++)
    if (yonaPathIsSep(Path[I]))
      Base = Path + I + 1;
  const char *Dot = NULL;
  for (const char *P = Base; *P; P++)
    if (*P == '.')
      Dot = P;
  size_t StemLen = Dot ? (size_t)(Dot - Path) : Len;
  size_t Elen = strlen(Ext);
  char *R =
      (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, StemLen + Elen + 1);
  memcpy(R, Path, StemLen);
  memcpy(R + StemLen, Ext, Elen + 1);
  return R;
}

int64_t YonaStdPathIsAbsolute(const char *Path) {
  if (!Path || !Path[0])
    return 0;
  if (yonaPathIsSep(Path[0]))
    return 1;
#ifdef _WIN32
  if (((Path[0] >= 'A' && Path[0] <= 'Z') ||
       (Path[0] >= 'a' && Path[0] <= 'z')) &&
      Path[1] == ':')
    return 1;
#endif
  return 0;
}

/* ===== Std\FloatMath — floating-point math (wrappers for math.h) ===== */

double YonaStdFloatMathSqrt(double X) { return sqrt(X); }
double YonaStdFloatMathSin(double X) { return sin(X); }
double YonaStdFloatMathCos(double X) { return cos(X); }
double YonaStdFloatMathTan(double X) { return tan(X); }
double YonaStdFloatMathLog(double X) { return log(X); }
double YonaStdFloatMathExp(double X) { return exp(X); }
double YonaStdFloatMathFloor(double X) { return floor(X); }
double YonaStdFloatMathCeil(double X) { return ceil(X); }
double YonaStdFloatMathRound(double X) { return round(X); }
double YonaStdFloatMathPi(void) { return 3.14159265358979323846; }

/* ===== Std\Format — string formatting ===== */

/* Simple placeholder format: replace {} with arguments in order.
 * Takes a format string and a sequence of string arguments. */
const char *YonaStdFormatFormat(const char *Fmt, int64_t *Args) {
  int64_t Argc = YonaRuntimeSequenceLength(Args);
  int64_t Argi = 0;
  size_t Flen = strlen(Fmt);

  /* First pass: calculate output size */
  size_t OutSize = 0;
  for (size_t I = 0; I < Flen; I++) {
    if (Fmt[I] == '{' && I + 1 < Flen && Fmt[I + 1] == '}' && Argi < Argc) {
      const char *Arg =
          (const char *)(intptr_t)YonaRuntimeSequenceGet(Args, Argi);
      OutSize += strlen(Arg);
      Argi++;
      I++; /* skip } */
    } else {
      OutSize++;
    }
  }

  /* Second pass: build output */
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, OutSize + 1);
  Argi = 0;
  size_t J = 0;
  for (size_t I = 0; I < Flen; I++) {
    if (Fmt[I] == '{' && I + 1 < Flen && Fmt[I + 1] == '}' && Argi < Argc) {
      const char *Arg =
          (const char *)(intptr_t)YonaRuntimeSequenceGet(Args, Argi);
      size_t Alen = strlen(Arg);
      memcpy(R + J, Arg, Alen);
      J += Alen;
      Argi++;
      I++;
    } else {
      R[J++] = Fmt[I];
    }
  }
  R[J] = '\0';
  return R;
}

/* ===== Std\Json — recursive ADT parse/stringify (see runtime/json.c) ===== */

/* ===== Std\Crypto — hashing and random bytes ===== */

/* SHA-256 implementation (standalone, no openssl dependency) */
static const uint32_t Sha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define YONA_SH_A256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define YONA_SH_A256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define YONA_SH_A256_E_P0(x)                                                   \
  (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define YONA_SH_A256_E_P1(x)                                                   \
  (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define YONA_SH_A256_SI_G0(x)                                                  \
  (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define YONA_SH_A256_SI_G1(x)                                                  \
  (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

const char *YonaStdCryptoSha256(const char *Input) {
  size_t Len = strlen(Input);
  /* Padding */
  size_t NewLen = ((Len + 8) / 64 + 1) * 64;
  uint8_t *Msg = (uint8_t *)calloc(NewLen, 1);
  memcpy(Msg, Input, Len);
  Msg[Len] = 0x80;
  uint64_t Bits = (uint64_t)Len * 8;
  for (int I = 0; I < 8; I++)
    Msg[NewLen - 1 - I] = (uint8_t)(Bits >> (I * 8));

  uint32_t H[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  for (size_t Chunk = 0; Chunk < NewLen; Chunk += 64) {
    uint32_t W[64];
    for (int I = 0; I < 16; I++)
      W[I] = ((uint32_t)Msg[Chunk + I * 4] << 24) |
             ((uint32_t)Msg[Chunk + I * 4 + 1] << 16) |
             ((uint32_t)Msg[Chunk + I * 4 + 2] << 8) |
             (uint32_t)Msg[Chunk + I * 4 + 3];
    for (int I = 16; I < 64; I++)
      W[I] = YONA_SH_A256_SI_G1(W[I - 2]) + W[I - 7] +
             YONA_SH_A256_SI_G0(W[I - 15]) + W[I - 16];

    uint32_t A = H[0], B = H[1], C = H[2], D = H[3], E = H[4], F = H[5],
             G = H[6], Hh = H[7];
    for (int I = 0; I < 64; I++) {
      uint32_t T1 = Hh + YONA_SH_A256_E_P1(E) + YONA_SH_A256_CH(E, F, G) +
                    Sha256K[I] + W[I];
      uint32_t T2 = YONA_SH_A256_E_P0(A) + YONA_SH_A256_MAJ(A, B, C);
      Hh = G;
      G = F;
      F = E;
      E = D + T1;
      D = C;
      C = B;
      B = A;
      A = T1 + T2;
    }
    H[0] += A;
    H[1] += B;
    H[2] += C;
    H[3] += D;
    H[4] += E;
    H[5] += F;
    H[6] += G;
    H[7] += Hh;
  }
  free(Msg);

  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 65);
  for (int I = 0; I < 8; I++)
    snprintf(R + I * 8, 9, "%08x", H[I]);
  return R;
}

static int yonaCryptoRandomFill(uint8_t *Buffer, size_t Length) {
  if (Length == 0)
    return 1;
#if defined(_WIN32)
  typedef LONG(WINAPI * BcryptGenRandomFn)(void *, unsigned char *,
                                           unsigned long, unsigned long);
  HMODULE Library = LoadLibraryA("bcrypt.dll");
  if (!Library)
    return 0;
  BcryptGenRandomFn Generate =
      (BcryptGenRandomFn)(uintptr_t)GetProcAddress(Library, "BCryptGenRandom");
  const LONG Result =
      Generate ? Generate(NULL, Buffer, (unsigned long)Length, 0x00000002UL)
               : (LONG)-1;
  FreeLibrary(Library);
  return Result >= 0;
#else
  FILE *source = fopen("/dev/urandom", "rb");
  if (!source)
    return 0;
  const size_t read = fread(buffer, 1, length, source);
  fclose(source);
  return read == length;
#endif
}

void *YonaStdCryptoRandomBytes(int64_t N) {
  if (N < 0)
    N = 0;
  int64_t *Result = (int64_t *)YonaRuntimeByteArrayAllocate(N);
  uint8_t *Bytes = (uint8_t *)(Result + 1);
  if (!yonaCryptoRandomFill(Bytes, (size_t)N))
    memset(Bytes, 0, (size_t)N);
  return Result;
}

const char *YonaStdCryptoRandomHex(int64_t N) {
  if (N < 0)
    N = 0;
  char *Bytes = (char *)malloc((size_t)N);
  if (!yonaCryptoRandomFill((uint8_t *)Bytes, (size_t)N))
    memset(Bytes, 0, (size_t)N);
  char *R =
      (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, (size_t)N * 2 + 1);
  static const char Hex[] = "0123456789abcdef";
  for (int64_t I = 0; I < N; I++) {
    R[I * 2] = Hex[((uint8_t)Bytes[I] >> 4) & 0xF];
    R[I * 2 + 1] = Hex[(uint8_t)Bytes[I] & 0xF];
  }
  R[N * 2] = '\0';
  free(Bytes);
  return R;
}

const char *YonaStdCryptoUuid4(void) {
  uint8_t Bytes[16];
  if (!yonaCryptoRandomFill(Bytes, sizeof(Bytes)))
    memset(Bytes, 0, sizeof(Bytes));
  Bytes[6] = (Bytes[6] & 0x0f) | 0x40; /* version 4 */
  Bytes[8] = (Bytes[8] & 0x3f) | 0x80; /* variant 1 */
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 37);
  snprintf(
      R, 37,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      Bytes[0], Bytes[1], Bytes[2], Bytes[3], Bytes[4], Bytes[5], Bytes[6],
      Bytes[7], Bytes[8], Bytes[9], Bytes[10], Bytes[11], Bytes[12], Bytes[13],
      Bytes[14], Bytes[15]);
  return R;
}

/* ===== Std\Log — structured logging ===== */

static int64_t YonaLogLevel = 1; /* 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR */
static const char *YonaLogLevelNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};

void YonaStdLogSetLevel(int64_t Level) { YonaLogLevel = Level; }
int64_t YonaStdLogGetLevel(void) { return YonaLogLevel; }

static void yonaLogEmit(int64_t Level, const char *Msg) {
  if (Level < YonaLogLevel)
    return;
  time_t Now = time(NULL);
  struct tm *Tm = localtime(&Now);
  char Ts[20];
  strftime(Ts, sizeof(Ts), "%Y-%m-%d %H:%M:%S", Tm);
  fprintf(stderr, "[%s] %s: %s\n", Ts, YonaLogLevelNames[Level], Msg);
  fflush(stderr);
}

void YonaStdLogDebug(const char *Msg) { yonaLogEmit(0, Msg); }
void YonaStdLogInfo(const char *Msg) { yonaLogEmit(1, Msg); }
void YonaStdLogWarn(const char *Msg) { yonaLogEmit(2, Msg); }
void YonaStdLogError(const char *Msg) { yonaLogEmit(3, Msg); }

/* Std\List */
int64_t YonaStdListLength(int64_t *Seq) {
  return YonaRuntimeSequenceLength(Seq);
}
int64_t YonaStdListHead(int64_t *Seq) { return YonaRuntimeSequenceHead(Seq); }
int64_t *YonaStdListTail(int64_t *Seq) { return YonaRuntimeSequenceTail(Seq); }
int64_t *YonaStdListReverse(int64_t *Seq) {
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  int64_t *R = YonaRuntimeSequenceAllocate(Len);
  for (int64_t I = 0; I < Len; I++)
    YonaRuntimeSequenceSet(R, I, YonaRuntimeSequenceGet(Seq, Len - 1 - I));
  return R;
}

/* C loop-based foldl/foldr — no stack overflow on large sequences.
 * fn is a closure with arity 2: fn(acc, elem) -> new_acc
 * collection can be Seq (RC_TYPE_SEQ) or Iterator (RC_TYPE_ADT).
 * Detection via RC type tag in header[1] & 0xFF. */
typedef int64_t (*FoldFunction)(int64_t *Env, int64_t, int64_t);

/* Fold over an Iterator: call next() in a loop until None */
static int64_t foldlIterator(FoldFunction F, int64_t *Fn, int64_t Acc,
                             int64_t *IterAdt) {
  /* Iterator ADT layout: [tag, num_fields, heap_mask, closure_ptr] */
  int64_t *Closure = (int64_t *)(intptr_t)IterAdt[YONA_STDLIB_ADT_HEADER_SIZE];
  typedef int64_t (*NextFnT)(int64_t *);
  NextFnT Next = (NextFnT)(intptr_t)Closure[0];

  while (1) {
    int64_t Option = Next(Closure);
    int64_t *OptPtr = (int64_t *)(intptr_t)Option;
    /* Option ADT layout: [tag, num_fields, heap_mask, field0] */
    if (OptPtr[0] != 0) {
      YonaRuntimeRelease(OptPtr); /* free None */
      break;
    }
    int64_t Elem = OptPtr[YONA_STDLIB_ADT_HEADER_SIZE]; /* field0 */
    YonaRuntimeRelease(
        OptPtr); /* free Some wrapper (heap_mask=0, doesn't touch elem) */
    Acc = F(Fn, Acc, Elem);
  }
  return Acc;
}

/* Fold over a Seq: head/tail loop */
static int64_t foldlSeq(FoldFunction F, int64_t *Fn, int64_t Acc,
                        int64_t *Seq) {
  while (YonaRuntimeSequenceLength(Seq) > 0) {
    int64_t Elem = YonaRuntimeSequenceHead(Seq);
    Acc = F(Fn, Acc, Elem);
    Seq = YonaRuntimeSequenceTail(Seq);
  }
  return Acc;
}

/* Polymorphic foldl: works on Seq, Iterator, ByteArray, IntArray, FloatArray,
 * String. Detects collection type via RC type tag and dispatches to specialized
 * foldl. */
int64_t YonaStdListFoldl(int64_t *Fn, int64_t Acc, int64_t *Collection) {
  FoldFunction F = (FoldFunction)(intptr_t)Fn[0];
  /* Check RC type tag: header is at ptr - RC_HEADER_SIZE */
  int64_t *Header = Collection - YONA_RUNTIME_RC_HEADER_SIZE;
  int64_t TypeTag = Header[1] & 0xFF;
  if (TypeTag == YONA_RUNTIME_TYPE_ADT)
    return foldlIterator(F, Fn, Acc, Collection);
  if (TypeTag == YONA_RUNTIME_TYPE_INT_ARRAY)
    return YonaRuntimeIntArrayFoldLeft(Fn, Acc, Collection);
  if (TypeTag == YONA_RUNTIME_TYPE_FLOAT_ARRAY) {
    /* FloatArray uses double; we need to convert through int64 representation.
     * Cast i64 acc to double via memcpy, fold, cast back. */
    double Dacc;
    memcpy(&Dacc, &Acc, sizeof(double));
    double Dresult =
        YonaRuntimeFloatArrayFoldLeft(Fn, Dacc, (double *)Collection);
    int64_t Iresult;
    memcpy(&Iresult, &Dresult, sizeof(double));
    return Iresult;
  }
  if (TypeTag == YONA_RUNTIME_TYPE_BYTE_ARRAY) {
    int64_t Len = YonaRuntimeByteArrayLength(Collection);
    uint8_t *Data = (uint8_t *)(Collection + 1);
    for (int64_t I = 0; I < Len; I++)
      Acc = F(Fn, Acc, (int64_t)Data[I]);
    return Acc;
  }
  if (TypeTag == YONA_RUNTIME_TYPE_STRING) {
    const char *S = (const char *)Collection;
    for (; *S; S++)
      Acc = F(Fn, Acc, (int64_t)(unsigned char)*S);
    return Acc;
  }
  return foldlSeq(F, Fn, Acc, Collection);
}

int64_t YonaStdListFold(int64_t *Fn, int64_t Acc, int64_t *Collection) {
  return YonaStdListFoldl(Fn, Acc, Collection);
}

int64_t YonaStdListFoldr(int64_t *Fn, int64_t Acc, int64_t *Seq) {
  FoldFunction F = (FoldFunction)(intptr_t)Fn[0];
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  for (int64_t I = Len - 1; I >= 0; I--) {
    int64_t Elem = YonaRuntimeSequenceGet(Seq, I);
    Acc = F(Fn, Elem, Acc);
  }
  return Acc;
}

/* C loop-based map and filter */
int64_t *YonaStdListMap(int64_t *Fn, int64_t *Seq) {
  typedef int64_t (*MapFnT)(int64_t *Env, int64_t);
  MapFnT F = (MapFnT)(intptr_t)Fn[0];
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  int64_t *Result = YonaRuntimeSequenceAllocate(Len);
  for (int64_t I = 0; I < Len; I++) {
    int64_t Elem = YonaRuntimeSequenceGet(Seq, I);
    YonaRuntimeSequenceSet(Result, I, F(Fn, Elem));
  }
  return Result;
}

int64_t *YonaStdListFilter(int64_t *Fn, int64_t *Seq) {
  typedef int64_t (*PredFnT)(int64_t *Env, int64_t);
  PredFnT F = (PredFnT)(intptr_t)Fn[0];
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  int64_t *Result = YonaRuntimeSequenceAllocate(Len); /* over-allocate */
  int64_t Count = 0;
  for (int64_t I = 0; I < Len; I++) {
    int64_t Elem = YonaRuntimeSequenceGet(Seq, I);
    if (F(Fn, Elem)) {
      YonaRuntimeSequenceSet(Result, Count, Elem);
      Count++;
    }
  }
  Result[0] = Count; /* actual count */
  return Result;
}

int64_t YonaStdListSum(int64_t *Seq) {
  int64_t Total = 0;
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  for (int64_t I = 0; I < Len; I++)
    Total += YonaRuntimeSequenceGet(Seq, I);
  return Total;
}

int64_t YonaStdListProduct(int64_t *Seq) {
  int64_t Total = 1;
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  for (int64_t I = 0; I < Len; I++)
    Total *= YonaRuntimeSequenceGet(Seq, I);
  return Total;
}

/* Prelude aliases — always available without import */
int64_t YonaRuntimeFoldl(int64_t *Fn, int64_t Acc, int64_t *Seq) {
  return YonaStdListFoldl(Fn, Acc, Seq);
}
int64_t YonaRuntimeFoldr(int64_t *Fn, int64_t Acc, int64_t *Seq) {
  return YonaStdListFoldr(Fn, Acc, Seq);
}

/* Std\Types — type conversions */
int64_t YonaStdTypesToInt(const char *S) { return (int64_t)atoll(S); }
double YonaStdTypesToFloat(const char *S) { return atof(S); }
const char *YonaStdTypesIntToString(int64_t N) {
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 32);
  snprintf(R, 32, "%" PRId64, N);
  return R;
}
const char *YonaStdTypesFloatToString(double F) {
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 32);
  snprintf(R, 32, "%g", F);
  return R;
}
const char *YonaStdTypesBoolToString(int64_t B) {
  const char *Src = B ? "true" : "false";
  size_t Len = strlen(Src);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  memcpy(R, Src, Len + 1);
  return R;
}

/* ===== Primitive trait instances (Show, Eq, Ord, Hash) ===== */

const char *YonaRuntimeShowIntShow(int64_t N) {
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 32);
  snprintf(R, 32, "%" PRId64, N);
  return R;
}

const char *YonaRuntimeShowStringShow(const char *S) {
  /* Show for strings wraps in quotes: "hello" -> "\"hello\"" */
  size_t Len = strlen(S);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 3);
  R[0] = '"';
  memcpy(R + 1, S, Len);
  R[Len + 1] = '"';
  R[Len + 2] = '\0';
  return R;
}

const char *YonaRuntimeShowBoolShow(int64_t B) {
  const char *Src = B ? "true" : "false";
  size_t Len = strlen(Src);
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, Len + 1);
  memcpy(R, Src, Len + 1);
  return R;
}

const char *YonaRuntimeShowFloatShow(double F) {
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 32);
  snprintf(R, 32, "%g", F);
  return R;
}

int64_t YonaRuntimeEqIntEq(int64_t A, int64_t B) { return A == B ? 1 : 0; }

int64_t YonaRuntimeEqStringEq(const char *A, const char *B) {
  return strcmp(A, B) == 0 ? 1 : 0;
}

int64_t YonaRuntimeEqBoolEq(int64_t A, int64_t B) { return A == B ? 1 : 0; }

int64_t YonaRuntimeOrdIntCompare(int64_t A, int64_t B) {
  return (A < B) ? -1 : (A > B) ? 1 : 0;
}

int64_t YonaRuntimeHashIntHash(int64_t X) {
  /* splitmix64 finalizer */
  uint64_t H = (uint64_t)X;
  H = (H ^ (H >> 30)) * 0xbf58476d1ce4e5b9ULL;
  H = (H ^ (H >> 27)) * 0x94d049bb133111ebULL;
  return (int64_t)(H ^ (H >> 31));
}

int64_t YonaRuntimeHashStringHash(const char *S) {
  /* FNV-1a */
  uint64_t H = 14695981039346656037ULL;
  for (; *S; S++)
    H = (H ^ (uint64_t)(unsigned char)*S) * 1099511628211ULL;
  return (int64_t)H;
}

/* Float instances */
int64_t YonaRuntimeEqFloatEq(double A, double B) { return A == B ? 1 : 0; }
int64_t YonaRuntimeOrdFloatCompare(double A, double B) {
  return (A < B) ? -1 : (A > B) ? 1 : 0;
}
int64_t YonaRuntimeHashFloatHash(double F) {
  /* Eq identifies +0.0 and -0.0, so Hash must canonicalize their two IEEE
   * encodings to the same value. NaNs remain unconstrained because Eq NaN
   * is false. */
  if (F == 0.0)
    F = 0.0;
  uint64_t Bits;
  memcpy(&Bits, &F, sizeof(Bits));
  Bits = (Bits ^ (Bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
  Bits = (Bits ^ (Bits >> 27)) * 0x94d049bb133111ebULL;
  return (int64_t)(Bits ^ (Bits >> 31));
}

/* Bool instances */
int64_t YonaRuntimeOrdBoolCompare(int64_t A, int64_t B) {
  return (A < B) ? -1 : (A > B) ? 1 : 0;
}
int64_t YonaRuntimeHashBoolHash(int64_t B) { return B ? 1 : 0; }

/* String instances */
int64_t YonaRuntimeOrdStringCompare(const char *A, const char *B) {
  int R = strcmp(A, B);
  return (R < 0) ? -1 : (R > 0) ? 1 : 0;
}

/* Symbol instances */
const char *YonaRuntimeShowSymbolShow(int64_t SymId) {
  char *R = (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, 32);
  snprintf(R, 32, ":%" PRId64, SymId);
  return R;
}
int64_t YonaRuntimeEqSymbolEq(int64_t A, int64_t B) { return A == B ? 1 : 0; }
int64_t YonaRuntimeHashSymbolHash(int64_t S) { return S; }

/* ===== Array trait instance wrappers ===== */

int64_t YonaRuntimeArrayByteArrayLength(void *Arr) {
  return YonaRuntimeByteArrayLength(Arr);
}
int64_t YonaRuntimeArrayByteArrayGet(void *Arr, int64_t I) {
  return YonaRuntimeByteArrayGet(Arr, I);
}
int64_t YonaRuntimeArrayIntArrayLength(int64_t *Arr) {
  return YonaRuntimeIntArrayLength(Arr);
}
int64_t YonaRuntimeArrayIntArrayGet(int64_t *Arr, int64_t I) {
  return YonaRuntimeIntArrayGet(Arr, I);
}
int64_t YonaRuntimeArrayFloatArrayLength(double *Arr) {
  return (int64_t)YonaRuntimeFloatArrayLength(Arr);
}
double YonaRuntimeArrayFloatArrayGet(double *Arr, int64_t I) {
  return YonaRuntimeFloatArrayGet(Arr, I);
}
int64_t YonaRuntimeArraySeqLength(int64_t *Arr) {
  return YonaRuntimeSequenceLength(Arr);
}
int64_t YonaRuntimeArraySeqGet(int64_t *Arr, int64_t I) {
  return YonaRuntimeSequenceGetOwned(Arr, I);
}
int64_t YonaRuntimeArrayStringLength(const char *Arr) {
  extern int64_t YonaStdStringLength(const char *S);
  return YonaStdStringLength(Arr);
}
int64_t YonaRuntimeArrayStringGet(const char *Arr, int64_t I) {
  return YonaStdStringCharAt(Arr, I);
}

/* seq_head and seq_tail are in runtime/seq.c */

/* Async runtime: thread pool, promises, await */
/* Concurrency and closures are explicit component sources. */

/* UTF-8 ↔ LSP UTF-16 positions (documented C ABI + Std\Utf16) */
