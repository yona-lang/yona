#ifndef YONA_RUNTIME_COLLECTIONS_SET_H
#define YONA_RUNTIME_COLLECTIONS_SET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Allocate an empty persistent set. CapacityHint is accepted for callers
/// that already know an expected size; the HAMT grows on demand. The caller
/// owns the returned reference.
int64_t *YonaRuntimeSetAllocate(int64_t CapacityHint);

/// Mark whether set elements are reference-counted heap values. The set must
/// be uniquely owned while its descriptor flags change.
void YonaRuntimeSetSetHeap(int64_t *Set, int64_t ElementsAreHeap);

/// Insert an element, consuming Set and returning the updated persistent set.
int64_t *YonaRuntimeSetInsert(int64_t *Set, int64_t Element);

int64_t YonaRuntimeSetContains(int64_t *Set, int64_t Element);
int64_t YonaRuntimeSetSize(int64_t *Set);

/// Return an owned sequence containing the set elements.
int64_t *YonaRuntimeSetElements(int64_t *Set);

/// Persistent set operations consume Left and borrow Right.
int64_t *YonaRuntimeSetUnion(int64_t *Left, int64_t *Right);
int64_t *YonaRuntimeSetIntersection(int64_t *Left, int64_t *Right);
int64_t *YonaRuntimeSetDifference(int64_t *Left, int64_t *Right);
void YonaRuntimePrintSet(int64_t *Set);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_COLLECTIONS_SET_H */
