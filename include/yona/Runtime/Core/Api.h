#ifndef YONA_RUNTIME_CORE_API_H
#define YONA_RUNTIME_CORE_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Allocate a reference-counted payload with the supplied runtime type tag.
/// Returns null on allocation failure. The caller owns one reference and must
/// eventually pass it to `YonaRuntimeRelease`. Thread-safe.
void *YonaRuntimeAllocate(int64_t TypeTag, size_t PayloadBytes);

/// Add or remove an owning reference. Null pointers are accepted. Both
/// operations are thread-safe; release destroys the value at the zero count.
void YonaRuntimeRetain(void *Value);
void YonaRuntimeRelease(void *Value);

/// Allocate a reference-counted UTF-8 buffer. The WithLength form records the
/// logical byte length separately from capacity. The caller owns the result.
void *YonaRuntimeAllocateString(size_t Bytes);
void *YonaRuntimeAllocateStringWithLength(size_t Bytes, size_t StringLength);
int64_t YonaRuntimeStringLength(const char *String);

/// Allocate a reference-counted native state block. Finalize is invoked once
/// immediately before the block is reclaimed. The caller owns the result.
void *YonaRuntimeNativeStateAllocate(size_t Bytes,
                                     void (*Finalize)(void *State));

void *YonaRuntimeAdtAllocate(int64_t Tag, int64_t FieldCount);
int64_t YonaRuntimeAdtGetTag(void *Value);
int64_t YonaRuntimeAdtGetField(void *Value, int64_t Index);
void YonaRuntimeAdtSetField(void *Value, int64_t Index, int64_t Field);
void YonaRuntimeAdtSetHeapMask(void *Value, int64_t Mask);

void *YonaRuntimeTupleAllocate(int64_t ElementCount);
int64_t YonaRuntimeTupleGet(void *Tuple, int64_t Index);
void YonaRuntimeTupleSet(void *Tuple, int64_t Index, int64_t Value);
void YonaRuntimeTupleSetHeapMask(void *Tuple, int64_t Mask);

void *YonaRuntimeClosureCreate(void *Function, int64_t ReturnType,
                               int64_t Arity, int64_t CaptureCount);
void YonaRuntimeClosureSetCapture(void *Closure, int64_t Index, int64_t Value);
int64_t YonaRuntimeClosureGetCapture(void *Closure, int64_t Index);
void YonaRuntimeClosureSetHeapMask(void *Closure, int64_t Mask);
void YonaRuntimeClosureSetBorrowMask(void *Closure, int64_t Mask);

void YonaRuntimeRaise(int64_t Symbol, const char *Message);
int64_t YonaRuntimeGetExceptionSymbol(void);
const char *YonaRuntimeGetExceptionMessage(void);

void *YonaRuntimeByteArrayFromString(const char *String);
int64_t *YonaRuntimeIntArrayAllocate(int64_t Count);
double *YonaRuntimeFloatArrayAllocate(int64_t Count);
int64_t YonaRuntimeFloatArrayLength(double *Array);
void YonaRuntimePrintHeapValue(int64_t Value);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_CORE_API_H */
