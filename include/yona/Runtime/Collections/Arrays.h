#ifndef YONA_RUNTIME_COLLECTIONS_ARRAYS_H
#define YONA_RUNTIME_COLLECTIONS_ARRAYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *YonaRuntimeByteArrayAllocate(int64_t Size);
int64_t YonaRuntimeByteArrayLength(void *Bytes);
int64_t YonaRuntimeByteArrayGet(void *Bytes, int64_t Index);
void YonaRuntimeByteArraySet(void *Bytes, int64_t Index, int64_t Value);
void *YonaRuntimeByteArrayConcatenate(void *A, void *B);
void *YonaRuntimeByteArraySlice(void *Bytes, int64_t Start, int64_t Length);
void *YonaRuntimeByteArrayFromString(const char *String);
const char *YonaRuntimeByteArrayToString(void *Bytes);
void *YonaRuntimeByteArrayFromSequence(int64_t *Sequence);
int64_t *YonaRuntimeByteArrayToSequence(void *Bytes);
void YonaRuntimePrintByteArray(void *Bytes);

int64_t *YonaRuntimeIntArrayAllocate(int64_t Count);
int64_t YonaRuntimeIntArrayLength(int64_t *Array);
int64_t YonaRuntimeIntArrayGet(int64_t *Array, int64_t Index);
void YonaRuntimeIntArraySet(int64_t *Array, int64_t Index, int64_t Value);
int64_t YonaRuntimeIntArrayHead(int64_t *Array);
int64_t *YonaRuntimeIntArrayTail(int64_t *Array);
int64_t *YonaRuntimeIntArrayPrepend(int64_t Elem, int64_t *Array);
int64_t *YonaRuntimeIntArrayJoin(int64_t *A, int64_t *B);
int64_t *YonaRuntimeIntArraySlice(int64_t *Array, int64_t Start,
                                  int64_t Length);
int64_t *YonaRuntimeIntArrayMap(int64_t *Fn, int64_t *Array);
int64_t YonaRuntimeIntArrayFoldLeft(int64_t *Fn, int64_t Acc, int64_t *Array);
int64_t *YonaRuntimeIntArrayFilter(int64_t *Fn, int64_t *Array);
int64_t *YonaRuntimeIntArrayFromSequence(int64_t *Sequence);
int64_t *YonaRuntimeIntArrayToSequence(int64_t *Array);
void YonaRuntimePrintIntArray(int64_t *Array);

double *YonaRuntimeFloatArrayAllocate(int64_t Count);
int64_t YonaRuntimeFloatArrayLength(double *Array);
double YonaRuntimeFloatArrayGet(double *Array, int64_t Index);
void YonaRuntimeFloatArraySet(double *Array, int64_t Index, double Value);
double YonaRuntimeFloatArrayHead(double *Array);
double *YonaRuntimeFloatArrayTail(double *Array);
double *YonaRuntimeFloatArrayPrepend(double Elem, double *Array);
double *YonaRuntimeFloatArrayJoin(double *A, double *B);
double *YonaRuntimeFloatArrayMap(int64_t *Fn, double *Array);
double YonaRuntimeFloatArrayFoldLeft(int64_t *Fn, double Acc, double *Array);
void YonaRuntimePrintFloatArray(double *Array);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_COLLECTIONS_ARRAYS_H */
