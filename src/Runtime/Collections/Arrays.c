/*
 * Persistent unboxed array and byte-buffer collections.
 */

#include "yona/Runtime/Collections/Arrays.h"

#include "Runtime/Core/Internal.h"
#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===== Persistent Seq ===== */

/* ===== Bytes — length-prefixed byte buffer ===== */
/*
 * Layout: [rc_header][length: i64][byte0, byte1, ...]
 *                                  ^-- returned pointer
 * Unlike strings, Bytes can contain \0 and arbitrary binary data.
 * Length is stored at ptr[-1] (the i64 before the data pointer... no,
 * actually we store length at index 0 of the payload, same as SEQ).
 *
 * Actual layout: YonaRuntimeAllocate returns payload pointer.
 *   payload[0] = length (as i64)
 *   payload[1..] = bytes (packed as uint8_t, but stored after the i64 length)
 *
 * We use the same pattern as sequences: first i64 is length, then data.
 */

/* Allocate a Bytes buffer of the given size (uninitialized) */
void *YonaRuntimeByteArrayAllocate(int64_t Size) {
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_BYTE_ARRAY,
                                                sizeof(int64_t) + (size_t)Size);
  Buf[0] = Size;
  return Buf;
}

/* Get length of a Bytes buffer */
int64_t YonaRuntimeByteArrayLength(void *Bytes) {
  return ((int64_t *)Bytes)[0];
}

/* Get byte at index (returns 0-255) */
int64_t YonaRuntimeByteArrayGet(void *Bytes, int64_t Index) {
  int64_t Len = ((int64_t *)Bytes)[0];
  if (Index < 0 || Index >= Len)
    return 0;
  uint8_t *Data = (uint8_t *)((int64_t *)Bytes + 1);
  return (int64_t)Data[Index];
}

/* Set byte at index */
void YonaRuntimeByteArraySet(void *Bytes, int64_t Index, int64_t Value) {
  int64_t Len = ((int64_t *)Bytes)[0];
  if (Index < 0 || Index >= Len)
    return;
  uint8_t *Data = (uint8_t *)((int64_t *)Bytes + 1);
  Data[Index] = (uint8_t)(Value & 0xFF);
}

/* Concatenate two Bytes buffers */
void *YonaRuntimeByteArrayConcatenate(void *A, void *B) {
  int64_t LenA = ((int64_t *)A)[0];
  int64_t LenB = ((int64_t *)B)[0];
  int64_t *Result = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)(LenA + LenB));
  Result[0] = LenA + LenB;
  uint8_t *Dest = (uint8_t *)(Result + 1);
  memcpy(Dest, (uint8_t *)((int64_t *)A + 1), (size_t)LenA);
  memcpy(Dest + LenA, (uint8_t *)((int64_t *)B + 1), (size_t)LenB);
  return Result;
}

/* Slice: bytes[start..start+len] */
void *YonaRuntimeByteArraySlice(void *Bytes, int64_t Start, int64_t Len) {
  int64_t Total = ((int64_t *)Bytes)[0];
  if (Start < 0)
    Start = 0;
  if (Start + Len > Total)
    Len = Total - Start;
  if (Len <= 0)
    return YonaRuntimeByteArrayAllocate(0);
  int64_t *Result = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)Len);
  Result[0] = Len;
  uint8_t *Src = (uint8_t *)((int64_t *)Bytes + 1) + Start;
  memcpy((uint8_t *)(Result + 1), Src, (size_t)Len);
  return Result;
}

/* Convert String to Bytes (copies, no null terminator in output) */
void *YonaRuntimeByteArrayFromString(const char *S) {
  size_t Len = strlen(S);
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_BYTE_ARRAY,
                                                sizeof(int64_t) + Len);
  Buf[0] = (int64_t)Len;
  memcpy((uint8_t *)(Buf + 1), S, Len);
  return Buf;
}

/* Convert Bytes to String (adds null terminator) */
const char *YonaRuntimeByteArrayToString(void *Bytes) {
  int64_t Len = ((int64_t *)Bytes)[0];
  char *S =
      (char *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_STRING, (size_t)Len + 1);
  memcpy(S, (uint8_t *)((int64_t *)Bytes + 1), (size_t)Len);
  S[Len] = '\0';
  return S;
}

/* Create Bytes from a list of integers (each 0-255) */
void *YonaRuntimeByteArrayFromSequence(int64_t *Seq) {
  int64_t Len = Seq[0];
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(YONA_RUNTIME_TYPE_BYTE_ARRAY,
                                                sizeof(int64_t) + (size_t)Len);
  Buf[0] = Len;
  uint8_t *Data = (uint8_t *)(Buf + 1);
  for (int64_t I = 0; I < Len; I++)
    Data[I] = (uint8_t)(YonaRuntimeSequenceGet(Seq, I) & 0xFF);
  return Buf;
}

/* Convert Bytes to a list of integers (each 0-255) */
int64_t *YonaRuntimeByteArrayToSequence(void *Bytes) {
  int64_t Len = ((int64_t *)Bytes)[0];
  int64_t *Seq = YonaRuntimeSequenceAllocate(Len);
  uint8_t *Data = (uint8_t *)((int64_t *)Bytes + 1);
  for (int64_t I = 0; I < Len; I++)
    YonaRuntimeSequenceSet(Seq, I, (int64_t)Data[I]);
  return Seq;
}

/* Print Bytes as hex for debugging */
void YonaRuntimePrintByteArray(void *Bytes) {
  int64_t Len = ((int64_t *)Bytes)[0];
  uint8_t *Data = (uint8_t *)((int64_t *)Bytes + 1);
  printf("<<");
  for (int64_t I = 0; I < Len; I++) {
    if (I > 0)
      printf(", ");
    printf("%d", Data[I]);
  }
  printf(">>");
}

/* Std\Bytes module aliases */
void *YonaStdByteArrayAlloc(int64_t S) {
  return YonaRuntimeByteArrayAllocate(S);
}
int64_t YonaStdByteArrayLength(void *B) {
  return YonaRuntimeByteArrayLength(B);
}
int64_t YonaStdByteArrayGet(void *B, int64_t I) {
  return YonaRuntimeByteArrayGet(B, I);
}
void YonaStdByteArraySet(void *B, int64_t I, int64_t V) {
  YonaRuntimeByteArraySet(B, I, V);
}
void *YonaStdByteArrayConcat(void *A, void *B) {
  return YonaRuntimeByteArrayConcatenate(A, B);
}
void *YonaStdByteArraySlice(void *B, int64_t S, int64_t L) {
  return YonaRuntimeByteArraySlice(B, S, L);
}
void *YonaStdByteArrayFromString(const char *S) {
  return YonaRuntimeByteArrayFromString(S);
}
const char *YonaStdByteArrayToString(void *B) {
  return YonaRuntimeByteArrayToString(B);
}
void *YonaStdByteArrayFromSeq(int64_t *S) {
  return YonaRuntimeByteArrayFromSequence(S);
}
int64_t *YonaStdByteArrayToSeq(void *B) {
  return YonaRuntimeByteArrayToSequence(B);
}
int64_t YonaStdByteArrayHead(void *B) { return YonaRuntimeByteArrayGet(B, 0); }
void *YonaStdByteArrayTail(void *B) {
  int64_t Len = YonaRuntimeByteArrayLength(B);
  return (Len <= 1) ? YonaRuntimeByteArrayAllocate(0)
                    : YonaRuntimeByteArraySlice(B, 1, Len - 1);
}
void *YonaStdByteArrayJoin(void *A, void *B) {
  return YonaRuntimeByteArrayConcatenate(A, B);
}
int64_t YonaStdByteArrayFoldl(int64_t *Fn, int64_t Acc, void *B) {
  int64_t Len = YonaRuntimeByteArrayLength(B);
  typedef int64_t (*FoldFunction)(int64_t *, int64_t, int64_t);
  FoldFunction F = (FoldFunction)(intptr_t)Fn[0];
  uint8_t *Data = (uint8_t *)((int64_t *)B + 1);
  for (int64_t I = 0; I < Len; I++)
    Acc = F(Fn, Acc, (int64_t)Data[I]);
  return Acc;
}
void *YonaStdByteArrayMap(int64_t *Fn, void *B) {
  int64_t Len = YonaRuntimeByteArrayLength(B);
  void *Result = YonaRuntimeByteArrayAllocate(Len);
  typedef int64_t (*MapFnT)(int64_t *, int64_t);
  MapFnT F = (MapFnT)(intptr_t)Fn[0];
  uint8_t *Src = (uint8_t *)((int64_t *)B + 1);
  uint8_t *Dst = (uint8_t *)((int64_t *)Result + 1);
  for (int64_t I = 0; I < Len; I++)
    Dst[I] = (uint8_t)F(Fn, (int64_t)Src[I]);
  return Result;
}

/* ===== IntArray — contiguous unboxed int64_t[] ===== */
/* Layout: [count: i64][elem0, elem1, ...] — no per-element RC. */

int64_t *YonaRuntimeIntArrayAllocate(int64_t Count) {
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_INT_ARRAY,
      sizeof(int64_t) + (size_t)Count * sizeof(int64_t));
  Buf[0] = Count;
  return Buf;
}

int64_t YonaRuntimeIntArrayLength(int64_t *Arr) { return Arr[0]; }
int64_t YonaRuntimeIntArrayGet(int64_t *Arr, int64_t I) { return Arr[1 + I]; }

void YonaRuntimeIntArraySet(int64_t *Arr, int64_t I, int64_t V) {
  Arr[1 + I] = V;
}

int64_t YonaRuntimeIntArrayHead(int64_t *Arr) { return Arr[1]; }

/* tail: returns a slice view (new array with offset data). O(n) copy for now.
 */
int64_t *YonaRuntimeIntArrayTail(int64_t *Arr) {
  int64_t Len = Arr[0];
  if (Len <= 1)
    return YonaRuntimeIntArrayAllocate(0);
  int64_t NewLen = Len - 1;
  int64_t *Result = YonaRuntimeIntArrayAllocate(NewLen);
  memcpy(Result + 1, Arr + 2, (size_t)NewLen * sizeof(int64_t));
  return Result;
}

int64_t *YonaRuntimeIntArrayPrepend(int64_t Elem, int64_t *Arr) {
  int64_t Len = Arr[0];
  int64_t *Result = YonaRuntimeIntArrayAllocate(Len + 1);
  Result[1] = Elem;
  memcpy(Result + 2, Arr + 1, (size_t)Len * sizeof(int64_t));
  return Result;
}

int64_t *YonaRuntimeIntArrayJoin(int64_t *A, int64_t *B) {
  int64_t La = A[0], Lb = B[0];
  int64_t *Result = YonaRuntimeIntArrayAllocate(La + Lb);
  memcpy(Result + 1, A + 1, (size_t)La * sizeof(int64_t));
  memcpy(Result + 1 + La, B + 1, (size_t)Lb * sizeof(int64_t));
  return Result;
}

int64_t *YonaRuntimeIntArraySlice(int64_t *Arr, int64_t Start, int64_t Len) {
  int64_t *Result = YonaRuntimeIntArrayAllocate(Len);
  memcpy(Result + 1, Arr + 1 + Start, (size_t)Len * sizeof(int64_t));
  return Result;
}

int64_t *YonaRuntimeIntArrayMap(int64_t *Fn, int64_t *Arr) {
  int64_t Len = Arr[0];
  int64_t *Result = YonaRuntimeIntArrayAllocate(Len);
  typedef int64_t (*MapFnT)(int64_t *, int64_t);
  MapFnT F = (MapFnT)(intptr_t)Fn[0];
  for (int64_t I = 0; I < Len; I++)
    Result[1 + I] = F(Fn, Arr[1 + I]);
  return Result;
}

int64_t YonaRuntimeIntArrayFoldLeft(int64_t *Fn, int64_t Acc, int64_t *Arr) {
  int64_t Len = Arr[0];
  typedef int64_t (*FoldFunction)(int64_t *, int64_t, int64_t);
  FoldFunction F = (FoldFunction)(intptr_t)Fn[0];
  for (int64_t I = 0; I < Len; I++)
    Acc = F(Fn, Acc, Arr[1 + I]);
  return Acc;
}

int64_t *YonaRuntimeIntArrayFilter(int64_t *Fn, int64_t *Arr) {
  int64_t Len = Arr[0];
  typedef int64_t (*PredFnT)(int64_t *, int64_t);
  PredFnT P = (PredFnT)(intptr_t)Fn[0];
  /* Two-pass: count matches, then fill */
  int64_t Count = 0;
  for (int64_t I = 0; I < Len; I++)
    if (P(Fn, Arr[1 + I]))
      Count++;
  int64_t *Result = YonaRuntimeIntArrayAllocate(Count);
  int64_t J = 0;
  for (int64_t I = 0; I < Len; I++)
    if (P(Fn, Arr[1 + I]))
      Result[1 + J++] = Arr[1 + I];
  return Result;
}

int64_t *YonaRuntimeIntArrayFromSequence(int64_t *Seq) {
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  int64_t *Result = YonaRuntimeIntArrayAllocate(Len);
  for (int64_t I = 0; I < Len; I++)
    Result[1 + I] = YonaRuntimeSequenceGet(Seq, I);
  return Result;
}

int64_t *YonaRuntimeIntArrayToSequence(int64_t *Arr) {
  int64_t Len = Arr[0];
  int64_t *Seq = YonaRuntimeSequenceAllocate(Len);
  for (int64_t I = 0; I < Len; I++)
    YonaRuntimeSequenceSet(Seq, I, Arr[1 + I]);
  return Seq;
}

void YonaRuntimePrintIntArray(int64_t *Arr) {
  int64_t Len = Arr[0];
  printf("IntArray[");
  for (int64_t I = 0; I < Len; I++) {
    if (I > 0)
      printf(", ");
    printf("%" PRId64, Arr[1 + I]);
  }
  printf("]");
}

/* Std\IntArray wrappers */
int64_t *YonaStdIntArrayAlloc(int64_t N) {
  return YonaRuntimeIntArrayAllocate(N);
}
int64_t *YonaStdIntArrayFill(int64_t N, int64_t V) {
  int64_t *Arr = YonaRuntimeIntArrayAllocate(N);
  for (int64_t I = 0; I < N; I++)
    Arr[1 + I] = V;
  return Arr;
}
int64_t YonaStdIntArrayLength(int64_t *A) {
  return YonaRuntimeIntArrayLength(A);
}
int64_t YonaStdIntArrayGet(int64_t *A, int64_t I) {
  return YonaRuntimeIntArrayGet(A, I);
}
int64_t *YonaStdIntArraySet(int64_t *A, int64_t I, int64_t V) {
  /* Persistent: copy-on-write */
  int64_t Len = A[0];
  int64_t *Result = YonaRuntimeIntArrayAllocate(Len);
  memcpy(Result + 1, A + 1, (size_t)Len * sizeof(int64_t));
  Result[1 + I] = V;
  return Result;
}
int64_t YonaStdIntArrayHead(int64_t *A) { return YonaRuntimeIntArrayHead(A); }
int64_t *YonaStdIntArrayTail(int64_t *A) { return YonaRuntimeIntArrayTail(A); }
int64_t *YonaStdIntArrayCons(int64_t E, int64_t *A) {
  return YonaRuntimeIntArrayPrepend(E, A);
}
int64_t *YonaStdIntArrayJoin(int64_t *A, int64_t *B) {
  return YonaRuntimeIntArrayJoin(A, B);
}
int64_t *YonaStdIntArraySlice(int64_t *A, int64_t S, int64_t L) {
  return YonaRuntimeIntArraySlice(A, S, L);
}
int64_t *YonaStdIntArrayMap(int64_t *Fn, int64_t *A) {
  return YonaRuntimeIntArrayMap(Fn, A);
}
int64_t YonaStdIntArrayFoldl(int64_t *Fn, int64_t Acc, int64_t *A) {
  return YonaRuntimeIntArrayFoldLeft(Fn, Acc, A);
}
int64_t *YonaStdIntArrayFilter(int64_t *Fn, int64_t *A) {
  return YonaRuntimeIntArrayFilter(Fn, A);
}
int64_t *YonaStdIntArrayFromSeq(int64_t *S) {
  return YonaRuntimeIntArrayFromSequence(S);
}
int64_t *YonaStdIntArrayToSeq(int64_t *A) {
  return YonaRuntimeIntArrayToSequence(A);
}

/* Accelerated columnar backends used by Std\Gpu. */

/* ===== FloatArray — contiguous unboxed double[] ===== */
/* Layout: [count: i64][double0, double1, ...] */

double *YonaRuntimeFloatArrayAllocate(int64_t Count) {
  /* Store count as i64 before the double data */
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_FLOAT_ARRAY,
      sizeof(int64_t) + (size_t)Count * sizeof(double));
  Buf[0] = Count;
  return (double *)(Buf + 1); /* return pointer to first double */
}

/* Access count from the i64 before the double data */
static int64_t *floatArrayHeader(double *Arr) { return ((int64_t *)Arr) - 1; }

int64_t YonaRuntimeFloatArrayLength(double *Arr) {
  return floatArrayHeader(Arr)[0];
}
double YonaRuntimeFloatArrayGet(double *Arr, int64_t I) { return Arr[I]; }
void YonaRuntimeFloatArraySet(double *Arr, int64_t I, double V) { Arr[I] = V; }
double YonaRuntimeFloatArrayHead(double *Arr) { return Arr[0]; }

double *YonaRuntimeFloatArrayTail(double *Arr) {
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  if (Len <= 1)
    return YonaRuntimeFloatArrayAllocate(0);
  int64_t NewLen = Len - 1;
  double *Result = YonaRuntimeFloatArrayAllocate(NewLen);
  memcpy(Result, Arr + 1, (size_t)NewLen * sizeof(double));
  return Result;
}

double *YonaRuntimeFloatArrayPrepend(double Elem, double *Arr) {
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  double *Result = YonaRuntimeFloatArrayAllocate(Len + 1);
  Result[0] = Elem;
  memcpy(Result + 1, Arr, (size_t)Len * sizeof(double));
  return Result;
}

double *YonaRuntimeFloatArrayJoin(double *A, double *B) {
  int64_t La = YonaRuntimeFloatArrayLength(A);
  int64_t Lb = YonaRuntimeFloatArrayLength(B);
  double *Result = YonaRuntimeFloatArrayAllocate(La + Lb);
  memcpy(Result, A, (size_t)La * sizeof(double));
  memcpy(Result + La, B, (size_t)Lb * sizeof(double));
  return Result;
}

double *YonaRuntimeFloatArrayMap(int64_t *Fn, double *Arr) {
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  double *Result = YonaRuntimeFloatArrayAllocate(Len);
  typedef double (*MapFnT)(int64_t *, double);
  MapFnT F = (MapFnT)(intptr_t)Fn[0];
  for (int64_t I = 0; I < Len; I++)
    Result[I] = F(Fn, Arr[I]);
  return Result;
}

double YonaRuntimeFloatArrayFoldLeft(int64_t *Fn, double Acc, double *Arr) {
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  typedef double (*FoldFunction)(int64_t *, double, double);
  FoldFunction F = (FoldFunction)(intptr_t)Fn[0];
  for (int64_t I = 0; I < Len; I++)
    Acc = F(Fn, Acc, Arr[I]);
  return Acc;
}

void YonaRuntimePrintFloatArray(double *Arr) {
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  printf("FloatArray[");
  for (int64_t I = 0; I < Len; I++) {
    if (I > 0)
      printf(", ");
    printf("%g", Arr[I]);
  }
  printf("]");
}

/* Std\FloatArray wrappers */
double *YonaStdFloatArrayAlloc(int64_t N) {
  return YonaRuntimeFloatArrayAllocate(N);
}
double *YonaStdFloatArrayFill(int64_t N, double V) {
  double *Arr = YonaRuntimeFloatArrayAllocate(N);
  for (int64_t I = 0; I < N; I++)
    Arr[I] = V;
  return Arr;
}
int64_t YonaStdFloatArrayLength(double *A) {
  return YonaRuntimeFloatArrayLength(A);
}
double YonaStdFloatArrayGet(double *A, int64_t I) {
  return YonaRuntimeFloatArrayGet(A, I);
}
double *YonaStdFloatArraySet(double *A, int64_t I, double V) {
  int64_t Len = YonaRuntimeFloatArrayLength(A);
  double *Result = YonaRuntimeFloatArrayAllocate(Len);
  memcpy(Result, A, (size_t)Len * sizeof(double));
  Result[I] = V;
  return Result;
}
double YonaStdFloatArrayHead(double *A) { return YonaRuntimeFloatArrayHead(A); }
double *YonaStdFloatArrayTail(double *A) {
  return YonaRuntimeFloatArrayTail(A);
}
double *YonaStdFloatArrayCons(double E, double *A) {
  return YonaRuntimeFloatArrayPrepend(E, A);
}
double *YonaStdFloatArrayJoin(double *A, double *B) {
  return YonaRuntimeFloatArrayJoin(A, B);
}
double *YonaStdFloatArrayMap(int64_t *Fn, double *A) {
  return YonaRuntimeFloatArrayMap(Fn, A);
}
double YonaStdFloatArrayFoldl(int64_t *Fn, double Acc, double *A) {
  return YonaRuntimeFloatArrayFoldLeft(Fn, Acc, A);
}
