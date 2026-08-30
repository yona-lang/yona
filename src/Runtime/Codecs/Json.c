/* Std\Json — recursive Json ADT parse/stringify.
 *
 * Layout (RC_TYPE_ADT): [tag, num_fields, heap_mask, fields…]
 *   JsonNull=0, JsonBool=1, JsonInt=2, JsonFloat=3,
 *   JsonString=4 (heap), JsonArray=5 (heap seq), JsonObject=6 (heap seq of
 * 2-tuples). parse returns Prelude Result (Ok Json | Err String).
 */

#include "yona/Runtime/Codecs/Json.h"

#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef YONA_JSON_MAX_DEPTH
#define YONA_JSON_MAX_DEPTH 64
#endif
#ifndef YONA_JSON_MAX_BYTES
#define YONA_JSON_MAX_BYTES (16u * 1024u * 1024u)
#endif

#ifndef RC_TYPE_ADT
#define YONA_RC_TYPE_ADT 4
#endif
#ifndef RC_TYPE_STRING
#define YONA_RC_TYPE_STRING 6
#endif
#ifndef RC_TYPE_TUPLE
#define YONA_RC_TYPE_TUPLE 9
#endif
#ifndef ADT_HDR_SIZE
#define YONA_ADT_HDR_SIZE 3
#endif

typedef struct {
  int64_t **Items;
  size_t N;
  size_t Cap;
} YonaJsonVec;

static int yonaJsonVecPush(YonaJsonVec *V, int64_t *X) {
  if (V->N == V->Cap) {
    size_t Ncap = V->Cap ? V->Cap * 2 : 8;
    if (Ncap > (1u << 20))
      return 0;
    int64_t **Ni = (int64_t **)realloc(V->Items, Ncap * sizeof(*Ni));
    if (!Ni)
      return 0;
    V->Items = Ni;
    V->Cap = Ncap;
  }
  V->Items[V->N++] = X;
  return 1;
}

static void yonaJsonVecDispose(YonaJsonVec *V) {
  for (size_t I = 0; I < V->N; I++)
    YonaRuntimeRelease(V->Items[I]);
  free(V->Items);
  V->Items = NULL;
  V->N = 0;
  V->Cap = 0;
}

static char *yonaJsonCopyStr(const char *S, size_t N) {
  char *R = (char *)YonaRuntimeAllocateStringWithLength(N + 1, N);
  memcpy(R, S, N);
  R[N] = '\0';
  return R;
}

static int64_t *yonaJsonAdt(int64_t Tag, int64_t Nfields, int64_t HeapMask) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_ADT, (YONA_ADT_HDR_SIZE + Nfields) * sizeof(int64_t));
  Adt[0] = Tag;
  Adt[1] = Nfields;
  Adt[2] = HeapMask;
  return Adt;
}

static int64_t yonaJsonResultOk(int64_t *Json) {
  int64_t *Adt = yonaJsonAdt(0, 1, 1);
  Adt[3] = (int64_t)(intptr_t)Json;
  return (int64_t)(intptr_t)Adt;
}

static int64_t yonaJsonResultErr(const char *Msg) {
  int64_t *Adt = yonaJsonAdt(1, 1, 1);
  Adt[3] = (int64_t)(intptr_t)yonaJsonCopyStr(Msg, strlen(Msg));
  return (int64_t)(intptr_t)Adt;
}

const char *YonaStdJsonStringifyString(const char *S) {
  const char *Src = S ? S : "";
  size_t Len = strlen(Src);
  char *R = (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, Len * 6 + 3);
  size_t J = 0;
  R[J++] = '"';
  for (size_t I = 0; I < Len; I++) {
    unsigned char C = (unsigned char)Src[I];
    switch (C) {
    case '"':
      R[J++] = '\\';
      R[J++] = '"';
      break;
    case '\\':
      R[J++] = '\\';
      R[J++] = '\\';
      break;
    case '\n':
      R[J++] = '\\';
      R[J++] = 'n';
      break;
    case '\r':
      R[J++] = '\\';
      R[J++] = 'r';
      break;
    case '\t':
      R[J++] = '\\';
      R[J++] = 't';
      break;
    default:
      R[J++] = (char)C;
      break;
    }
  }
  R[J++] = '"';
  R[J] = '\0';
  return R;
}

const char *YonaStdJsonStringifyBool(int64_t B) {
  const char *Src = B ? "true" : "false";
  return yonaJsonCopyStr(Src, strlen(Src));
}

const char *YonaStdJsonStringifyFloat(double F) {
  char Buf[64];
  int N = snprintf(Buf, sizeof(Buf), "%g", F);
  if (N < 0)
    N = 0;
  return yonaJsonCopyStr(Buf, (size_t)N);
}

const char *YonaStdJsonNull(void) { return yonaJsonCopyStr("null", 4); }

int64_t YonaStdJsonParseInt(const char *S) {
  if (!S)
    return 0;
  return (int64_t)strtoll(S, NULL, 10);
}

double YonaStdJsonParseFloat(const char *S) { return S ? atof(S) : 0.0; }

typedef struct {
  char *Buf;
  size_t Len;
  size_t Cap;
  int Ok;
} YonaJsonBuf;

static void yonaJsonBufInit(YonaJsonBuf *B) {
  B->Buf = (char *)malloc(64);
  B->Len = 0;
  B->Cap = B->Buf ? 64 : 0;
  B->Ok = B->Buf != NULL;
}

static void yonaJsonBufPut(YonaJsonBuf *B, const char *S, size_t N) {
  if (!B->Ok)
    return;
  if (B->Len + N + 1 > B->Cap) {
    size_t Ncap = B->Cap ? B->Cap : 64;
    while (Ncap < B->Len + N + 1) {
      if (Ncap > YONA_JSON_MAX_BYTES / 2) {
        B->Ok = 0;
        return;
      }
      Ncap *= 2;
    }
    char *Nb = (char *)realloc(B->Buf, Ncap);
    if (!Nb) {
      B->Ok = 0;
      return;
    }
    B->Buf = Nb;
    B->Cap = Ncap;
  }
  memcpy(B->Buf + B->Len, S, N);
  B->Len += N;
  B->Buf[B->Len] = '\0';
}

static void yonaJsonBufCstr(YonaJsonBuf *B, const char *S) {
  yonaJsonBufPut(B, S, strlen(S));
}

static void yonaJsonDump(int64_t *J, YonaJsonBuf *B, int Depth);

static void yonaJsonDump(int64_t *J, YonaJsonBuf *B, int Depth) {
  if (!B->Ok || !J || Depth > YONA_JSON_MAX_DEPTH) {
    B->Ok = 0;
    return;
  }
  int64_t Tag = J[0];
  switch (Tag) {
  case 0:
    yonaJsonBufCstr(B, "null");
    break;
  case 1:
    yonaJsonBufCstr(B, J[3] ? "true" : "false");
    break;
  case 2: {
    char Tmp[32];
    int N = snprintf(Tmp, sizeof(Tmp), "%" PRId64, J[3]);
    if (N > 0)
      yonaJsonBufPut(B, Tmp, (size_t)N);
    break;
  }
  case 3: {
    union {
      int64_t I;
      double D;
    } U;
    U.I = J[3];
    const char *Fs = YonaStdJsonStringifyFloat(U.D);
    yonaJsonBufCstr(B, Fs);
    YonaRuntimeRelease((void *)Fs);
    break;
  }
  case 4: {
    const char *Escaped =
        YonaStdJsonStringifyString((const char *)(intptr_t)J[3]);
    yonaJsonBufCstr(B, Escaped);
    YonaRuntimeRelease((void *)Escaped);
    break;
  }
  case 5: {
    int64_t *Seq = (int64_t *)(intptr_t)J[3];
    int64_t N = YonaRuntimeSequenceLength(Seq);
    yonaJsonBufCstr(B, "[");
    for (int64_t I = 0; I < N; I++) {
      if (I)
        yonaJsonBufCstr(B, ",");
      yonaJsonDump((int64_t *)(intptr_t)YonaRuntimeSequenceGet(Seq, I), B,
                   Depth + 1);
    }
    yonaJsonBufCstr(B, "]");
    break;
  }
  case 6: {
    int64_t *Seq = (int64_t *)(intptr_t)J[3];
    int64_t N = YonaRuntimeSequenceLength(Seq);
    yonaJsonBufCstr(B, "{");
    for (int64_t I = 0; I < N; I++) {
      if (I)
        yonaJsonBufCstr(B, ",");
      int64_t *Tup = (int64_t *)(intptr_t)YonaRuntimeSequenceGet(Seq, I);
      const char *Key = (const char *)(intptr_t)Tup[2];
      const char *Escaped = YonaStdJsonStringifyString(Key);
      yonaJsonBufCstr(B, Escaped);
      YonaRuntimeRelease((void *)Escaped);
      yonaJsonBufCstr(B, ":");
      yonaJsonDump((int64_t *)(intptr_t)Tup[3], B, Depth + 1);
    }
    yonaJsonBufCstr(B, "}");
    break;
  }
  default:
    yonaJsonBufCstr(B, "null");
    break;
  }
}

const char *YonaStdJsonStringify(int64_t JsonI64) {
  YonaJsonBuf B;
  yonaJsonBufInit(&B);
  if (JsonI64)
    yonaJsonDump((int64_t *)(intptr_t)JsonI64, &B, 0);
  else
    yonaJsonBufCstr(&B, "null");
  if (!B.Ok) {
    free(B.Buf);
    return yonaJsonCopyStr("null", 4);
  }
  char *R = yonaJsonCopyStr(B.Buf, B.Len);
  free(B.Buf);
  return R;
}

typedef struct {
  const char *S;
  size_t N;
  size_t I;
  int Depth;
  const char *Err;
} YonaJsp;

static void yonaJspSkip(YonaJsp *P) {
  while (P->I < P->N) {
    char C = P->S[P->I];
    if (C != ' ' && C != '\t' && C != '\n' && C != '\r')
      break;
    P->I++;
  }
}

static int yonaHex4(YonaJsp *P, uint32_t *Out) {
  if (P->I + 4 > P->N)
    return 0;
  uint32_t V = 0;
  for (int K = 0; K < 4; K++) {
    unsigned char C = (unsigned char)P->S[P->I + (size_t)K];
    uint32_t D;
    if (C >= '0' && C <= '9')
      D = (uint32_t)(C - '0');
    else if (C >= 'A' && C <= 'F')
      D = (uint32_t)(C - 'A' + 10);
    else if (C >= 'a' && C <= 'f')
      D = (uint32_t)(C - 'a' + 10);
    else
      return 0;
    V = (V << 4) | D;
  }
  P->I += 4;
  *Out = V;
  return 1;
}

static void yonaUtf8Put(YonaJsonBuf *B, uint32_t Cp) {
  char Tmp[4];
  size_t N = 0;
  if (Cp < 0x80) {
    Tmp[N++] = (char)Cp;
  } else if (Cp < 0x800) {
    Tmp[N++] = (char)(0xC0 | (Cp >> 6));
    Tmp[N++] = (char)(0x80 | (Cp & 0x3F));
  } else if (Cp < 0x10000) {
    Tmp[N++] = (char)(0xE0 | (Cp >> 12));
    Tmp[N++] = (char)(0x80 | ((Cp >> 6) & 0x3F));
    Tmp[N++] = (char)(0x80 | (Cp & 0x3F));
  } else {
    Tmp[N++] = (char)(0xF0 | (Cp >> 18));
    Tmp[N++] = (char)(0x80 | ((Cp >> 12) & 0x3F));
    Tmp[N++] = (char)(0x80 | ((Cp >> 6) & 0x3F));
    Tmp[N++] = (char)(0x80 | (Cp & 0x3F));
  }
  yonaJsonBufPut(B, Tmp, N);
}

static int64_t *yonaJspString(YonaJsp *P) {
  if (P->I >= P->N || P->S[P->I] != '"') {
    P->Err = "expected string";
    return NULL;
  }
  P->I++;
  YonaJsonBuf B;
  yonaJsonBufInit(&B);
  while (P->I < P->N) {
    unsigned char C = (unsigned char)P->S[P->I];
    if (C == '"') {
      P->I++;
      if (!B.Ok) {
        free(B.Buf);
        P->Err = "string too large";
        return NULL;
      }
      int64_t *Adt = yonaJsonAdt(4, 1, 1);
      Adt[3] = (int64_t)(intptr_t)yonaJsonCopyStr(B.Buf ? B.Buf : "", B.Len);
      free(B.Buf);
      return Adt;
    }
    if (C < 32) {
      free(B.Buf);
      P->Err = "unescaped control";
      return NULL;
    }
    if (C != '\\') {
      yonaJsonBufPut(&B, (const char *)&C, 1);
      P->I++;
      continue;
    }
    if (P->I + 1 >= P->N) {
      free(B.Buf);
      P->Err = "truncated escape";
      return NULL;
    }
    char E = P->S[P->I + 1];
    P->I += 2;
    switch (E) {
    case '"':
    case '\\':
    case '/':
      yonaJsonBufPut(&B, &E, 1);
      break;
    case 'b':
      yonaJsonBufPut(&B, "\b", 1);
      break;
    case 'f':
      yonaJsonBufPut(&B, "\f", 1);
      break;
    case 'n':
      yonaJsonBufPut(&B, "\n", 1);
      break;
    case 'r':
      yonaJsonBufPut(&B, "\r", 1);
      break;
    case 't':
      yonaJsonBufPut(&B, "\t", 1);
      break;
    case 'u': {
      uint32_t Hi = 0;
      if (!yonaHex4(P, &Hi)) {
        free(B.Buf);
        P->Err = "bad hex";
        return NULL;
      }
      if (Hi >= 0xD800 && Hi <= 0xDBFF && P->I + 2 < P->N &&
          P->S[P->I] == '\\' && P->S[P->I + 1] == 'u') {
        P->I += 2;
        uint32_t Lo = 0;
        if (!yonaHex4(P, &Lo) || Lo < 0xDC00 || Lo > 0xDFFF) {
          free(B.Buf);
          P->Err = "bad surrogate";
          return NULL;
        }
        yonaUtf8Put(&B, 0x10000u + ((Hi - 0xD800u) << 10) + (Lo - 0xDC00u));
      } else {
        yonaUtf8Put(&B, Hi);
      }
      break;
    }
    default:
      free(B.Buf);
      P->Err = "bad escape";
      return NULL;
    }
  }
  free(B.Buf);
  P->Err = "unterminated string";
  return NULL;
}

static int64_t *yonaJspValue(YonaJsp *P);

static int64_t *yonaJspNumber(YonaJsp *P) {
  size_t Start = P->I;
  if (P->I < P->N && P->S[P->I] == '-')
    P->I++;
  size_t Dig0 = P->I;
  while (P->I < P->N && P->S[P->I] >= '0' && P->S[P->I] <= '9')
    P->I++;
  if (P->I == Dig0) {
    P->Err = "bad number";
    return NULL;
  }
  int Isf = 0;
  if (P->I < P->N && P->S[P->I] == '.') {
    P->I++;
    size_t F0 = P->I;
    while (P->I < P->N && P->S[P->I] >= '0' && P->S[P->I] <= '9')
      P->I++;
    if (P->I == F0) {
      P->Err = "bad number";
      return NULL;
    }
    Isf = 1;
  }
  if (P->I < P->N && (P->S[P->I] == 'e' || P->S[P->I] == 'E')) {
    P->I++;
    if (P->I < P->N && (P->S[P->I] == '+' || P->S[P->I] == '-'))
      P->I++;
    size_t E0 = P->I;
    while (P->I < P->N && P->S[P->I] >= '0' && P->S[P->I] <= '9')
      P->I++;
    if (P->I == E0) {
      P->Err = "bad number";
      return NULL;
    }
    Isf = 1;
  }
  char *Tok = (char *)malloc(P->I - Start + 1);
  if (!Tok) {
    P->Err = "oom";
    return NULL;
  }
  memcpy(Tok, P->S + Start, P->I - Start);
  Tok[P->I - Start] = '\0';
  int64_t *Adt;
  if (Isf) {
    union {
      int64_t I;
      double D;
    } U;
    U.D = atof(Tok);
    Adt = yonaJsonAdt(3, 1, 0);
    Adt[3] = U.I;
  } else {
    Adt = yonaJsonAdt(2, 1, 0);
    Adt[3] = (int64_t)strtoll(Tok, NULL, 10);
  }
  free(Tok);
  return Adt;
}

static int64_t *yonaJspArray(YonaJsp *P) {
  P->I++;
  if (P->Depth >= YONA_JSON_MAX_DEPTH) {
    P->Err = "too deep";
    return NULL;
  }
  P->Depth++;
  YonaJsonVec Tmp = {NULL, 0, 0};
  yonaJspSkip(P);
  if (P->I < P->N && P->S[P->I] == ']') {
    P->I++;
    P->Depth--;
    int64_t *Seq = YonaRuntimeSequenceAllocate(0);
    int64_t *Adt = yonaJsonAdt(5, 1, 1);
    Adt[3] = (int64_t)(intptr_t)Seq;
    return Adt;
  }
  for (;;) {
    int64_t *V = yonaJspValue(P);
    if (!V) {
      yonaJsonVecDispose(&Tmp);
      return NULL;
    }
    if (!yonaJsonVecPush(&Tmp, V)) {
      YonaRuntimeRelease(V);
      yonaJsonVecDispose(&Tmp);
      P->Err = "array too long";
      return NULL;
    }
    yonaJspSkip(P);
    if (P->I >= P->N) {
      yonaJsonVecDispose(&Tmp);
      P->Err = "unterminated array";
      return NULL;
    }
    if (P->S[P->I] == ',') {
      P->I++;
      continue;
    }
    if (P->S[P->I] == ']') {
      P->I++;
      break;
    }
    yonaJsonVecDispose(&Tmp);
    P->Err = "expected comma or ]";
    return NULL;
  }
  P->Depth--;
  int64_t *Seq = YonaRuntimeSequenceAllocate((int64_t)Tmp.N);
  for (size_t I = 0; I < Tmp.N; I++)
    YonaRuntimeSequenceSet(Seq, (int64_t)I, (int64_t)(intptr_t)Tmp.Items[I]);
  free(Tmp.Items);
  YonaRuntimeSequenceSetHeap(Seq, 1);
  int64_t *Adt = yonaJsonAdt(5, 1, 1);
  Adt[3] = (int64_t)(intptr_t)Seq;
  return Adt;
}

static int64_t *yonaJspObject(YonaJsp *P) {
  P->I++;
  if (P->Depth >= YONA_JSON_MAX_DEPTH) {
    P->Err = "too deep";
    return NULL;
  }
  P->Depth++;
  YonaJsonVec Tmp = {NULL, 0, 0};
  yonaJspSkip(P);
  if (P->I < P->N && P->S[P->I] == '}') {
    P->I++;
    P->Depth--;
    int64_t *Seq = YonaRuntimeSequenceAllocate(0);
    int64_t *Adt = yonaJsonAdt(6, 1, 1);
    Adt[3] = (int64_t)(intptr_t)Seq;
    return Adt;
  }
  for (;;) {
    yonaJspSkip(P);
    int64_t *Ks = yonaJspString(P);
    if (!Ks) {
      yonaJsonVecDispose(&Tmp);
      return NULL;
    }
    yonaJspSkip(P);
    if (P->I >= P->N || P->S[P->I] != ':') {
      YonaRuntimeRelease(Ks);
      yonaJsonVecDispose(&Tmp);
      P->Err = "expected colon";
      return NULL;
    }
    P->I++;
    int64_t *V = yonaJspValue(P);
    if (!V) {
      YonaRuntimeRelease(Ks);
      yonaJsonVecDispose(&Tmp);
      return NULL;
    }
    int64_t *Tup =
        (int64_t *)YonaRuntimeAllocate(YONA_RC_TYPE_TUPLE, 4 * sizeof(int64_t));
    Tup[0] = 2;
    Tup[1] = 3;
    Tup[2] = Ks[3];
    Tup[3] = (int64_t)(intptr_t)V;
    /* Transfer the parsed key string from its temporary JsonString wrapper

     * * into the tuple. Clearing the wrapper mask prevents a double release. */
    Ks[2] = 0;
    YonaRuntimeRelease(Ks);
    if (!yonaJsonVecPush(&Tmp, Tup)) {
      YonaRuntimeRelease(Tup);
      yonaJsonVecDispose(&Tmp);
      P->Err = "object too large";
      return NULL;
    }
    yonaJspSkip(P);
    if (P->I >= P->N) {
      yonaJsonVecDispose(&Tmp);
      P->Err = "unterminated object";
      return NULL;
    }
    if (P->S[P->I] == ',') {
      P->I++;
      continue;
    }
    if (P->S[P->I] == '}') {
      P->I++;
      break;
    }
    yonaJsonVecDispose(&Tmp);
    P->Err = "expected comma or }";
    return NULL;
  }
  P->Depth--;
  int64_t *Seq = YonaRuntimeSequenceAllocate((int64_t)Tmp.N);
  for (size_t I = 0; I < Tmp.N; I++)
    YonaRuntimeSequenceSet(Seq, (int64_t)I, (int64_t)(intptr_t)Tmp.Items[I]);
  free(Tmp.Items);
  YonaRuntimeSequenceSetHeap(Seq, 1);
  int64_t *Adt = yonaJsonAdt(6, 1, 1);
  Adt[3] = (int64_t)(intptr_t)Seq;
  return Adt;
}

static int64_t *yonaJspValue(YonaJsp *P) {
  yonaJspSkip(P);
  if (P->I >= P->N) {
    P->Err = "unexpected eof";
    return NULL;
  }
  char C = P->S[P->I];
  if (C == 'n' && P->I + 4 <= P->N && memcmp(P->S + P->I, "null", 4) == 0) {
    P->I += 4;
    return yonaJsonAdt(0, 0, 0);
  }
  if (C == 't' && P->I + 4 <= P->N && memcmp(P->S + P->I, "true", 4) == 0) {
    P->I += 4;
    int64_t *Adt = yonaJsonAdt(1, 1, 0);
    Adt[3] = 1;
    return Adt;
  }
  if (C == 'f' && P->I + 5 <= P->N && memcmp(P->S + P->I, "false", 5) == 0) {
    P->I += 5;
    int64_t *Adt = yonaJsonAdt(1, 1, 0);
    Adt[3] = 0;
    return Adt;
  }
  if (C == '"')
    return yonaJspString(P);
  if (C == '-' || (C >= '0' && C <= '9'))
    return yonaJspNumber(P);
  if (C == '[')
    return yonaJspArray(P);
  if (C == '{')
    return yonaJspObject(P);
  P->Err = "expected value";
  return NULL;
}

int64_t YonaStdJsonParse(const char *S) {
  if (!S)
    return yonaJsonResultErr("unexpected eof");
  size_t N = (size_t)YonaRuntimeStringLength(S);
  if (N > YONA_JSON_MAX_BYTES)
    return yonaJsonResultErr("too large");
  YonaJsp P = {S, N, 0, 0, NULL};
  int64_t *V = yonaJspValue(&P);
  if (!V)
    return yonaJsonResultErr(P.Err ? P.Err : "parse error");
  yonaJspSkip(&P);
  if (P.I != P.N) {
    YonaRuntimeRelease(V);
    return yonaJsonResultErr("trailing junk");
  }
  return yonaJsonResultOk(V);
}
