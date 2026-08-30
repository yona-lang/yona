#include "yona/Runtime/Codecs/Utf16.h"

#include <string.h>

static uint32_t yonaUtf8Decode(const char *S, size_t N, size_t *I) {
  if (*I >= N)
    return 0;
  const unsigned char B0 = (unsigned char)S[*I];
  if (B0 < 0x80) {
    ++*I;
    return B0;
  }
  if ((B0 & 0xE0) == 0xC0 && *I + 1 < N) {
    const uint32_t Cp = ((uint32_t)(B0 & 0x1F) << 6) |
                        (uint32_t)((unsigned char)S[*I + 1] & 0x3F);
    *I += 2;
    return Cp;
  }
  if ((B0 & 0xF0) == 0xE0 && *I + 2 < N) {
    const uint32_t Cp = ((uint32_t)(B0 & 0x0F) << 12) |
                        ((uint32_t)((unsigned char)S[*I + 1] & 0x3F) << 6) |
                        (uint32_t)((unsigned char)S[*I + 2] & 0x3F);
    *I += 3;
    return Cp;
  }
  if ((B0 & 0xF8) == 0xF0 && *I + 3 < N) {
    const uint32_t Cp = ((uint32_t)(B0 & 0x07) << 18) |
                        ((uint32_t)((unsigned char)S[*I + 1] & 0x3F) << 12) |
                        ((uint32_t)((unsigned char)S[*I + 2] & 0x3F) << 6) |
                        (uint32_t)((unsigned char)S[*I + 3] & 0x3F);
    *I += 4;
    return Cp;
  }
  ++*I;
  return 0xFFFD;
}

static size_t yonaUtf16Width(uint32_t Cp) { return Cp > 0xFFFFu ? 2u : 1u; }

void YonaRuntimeUtf8OffsetToUtf16(const char *Utf8, size_t ByteCount,
                                  size_t ByteOffset, int64_t *OutLine,
                                  int64_t *OutCharacter) {
  int64_t Line = 0;
  int64_t Character = 0;
  if (!Utf8) {
    if (OutLine)
      *OutLine = 0;
    if (OutCharacter)
      *OutCharacter = 0;
    return;
  }
  if (ByteOffset > ByteCount)
    ByteOffset = ByteCount;
  size_t I = 0;
  while (I < ByteOffset) {
    if (Utf8[I] == '\n') {
      ++Line;
      Character = 0;
      ++I;
      continue;
    }
    if (Utf8[I] == '\r') {
      ++Line;
      Character = 0;
      ++I;
      if (I < ByteOffset && Utf8[I] == '\n')
        ++I;
      continue;
    }
    const uint32_t Cp = yonaUtf8Decode(Utf8, ByteCount, &I);
    Character += (int64_t)yonaUtf16Width(Cp);
  }
  if (OutLine)
    *OutLine = Line;
  if (OutCharacter)
    *OutCharacter = Character;
}

size_t YonaRuntimeUtf16PositionToUtf8(const char *Utf8, size_t ByteCount,
                                      int64_t Line, int64_t Character) {
  if (!Utf8)
    return 0;
  size_t I = 0;
  int64_t CurLine = 0;
  while (I < ByteCount && CurLine < Line) {
    if (Utf8[I] == '\n') {
      ++CurLine;
      ++I;
      continue;
    }
    if (Utf8[I] == '\r') {
      ++CurLine;
      ++I;
      if (I < ByteCount && Utf8[I] == '\n')
        ++I;
      continue;
    }
    yonaUtf8Decode(Utf8, ByteCount, &I);
  }
  int64_t Col = 0;
  while (I < ByteCount && Utf8[I] != '\n' && Utf8[I] != '\r' &&
         Col < Character) {
    const size_t Start = I;
    const uint32_t Cp = yonaUtf8Decode(Utf8, ByteCount, &I);
    Col += (int64_t)yonaUtf16Width(Cp);
    if (Col > Character)
      return Start;
  }
  return I;
}

int64_t YonaRuntimeUtf8OffsetToUtf16Line(const char *Utf8, int64_t ByteOffset) {
  int64_t Line = 0, Character = 0;
  const char *S = Utf8 ? Utf8 : "";
  const size_t N = strlen(S);
  const size_t Off = ByteOffset < 0 ? 0 : (size_t)ByteOffset;
  YonaRuntimeUtf8OffsetToUtf16(S, N, Off, &Line, &Character);
  return Line;
}

int64_t YonaRuntimeUtf8OffsetToUtf16Character(const char *Utf8,
                                              int64_t ByteOffset) {
  int64_t Line = 0, Character = 0;
  const char *S = Utf8 ? Utf8 : "";
  const size_t N = strlen(S);
  const size_t Off = ByteOffset < 0 ? 0 : (size_t)ByteOffset;
  YonaRuntimeUtf8OffsetToUtf16(S, N, Off, &Line, &Character);
  return Character;
}

int64_t YonaRuntimeUtf16PositionToUtf8Offset(const char *Utf8, int64_t Line,
                                             int64_t Character) {
  const char *S = Utf8 ? Utf8 : "";
  return (int64_t)YonaRuntimeUtf16PositionToUtf8(S, strlen(S), Line, Character);
}

int64_t YonaStdUtf16OffsetToLine(const char *Utf8, int64_t ByteOffset) {
  return YonaRuntimeUtf8OffsetToUtf16Line(Utf8, ByteOffset);
}

int64_t YonaStdUtf16OffsetToCharacter(const char *Utf8, int64_t ByteOffset) {
  return YonaRuntimeUtf8OffsetToUtf16Character(Utf8, ByteOffset);
}

int64_t YonaStdUtf16PositionToOffset(const char *Utf8, int64_t Line,
                                     int64_t Character) {
  return YonaRuntimeUtf16PositionToUtf8Offset(Utf8, Line, Character);
}
