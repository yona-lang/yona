/*
 * Native indexed-collection iterator adapters.
 */

#include "Runtime/Core/Internal.h"
#include "yona/Runtime/Collections/Arrays.h"
#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"

#include <stdint.h>
#include <string.h>

#define YONA_ITERATOR_ADT_HEADER_SIZE 3

typedef struct {
  void (*Finalize)(void *);
  void *Source;
  int64_t Index;
  int64_t Length;
  int Kind;
} IndexedIteratorState;

static void indexedIterFinalize(void *Raw) {
  IndexedIteratorState *St = (IndexedIteratorState *)Raw;
  if (St->Source)
    YonaRuntimeRelease(St->Source);
}

static int64_t indexedIterNext(int64_t *Env) {
  IndexedIteratorState *St = (IndexedIteratorState *)(intptr_t)Env[6];
  if (St->Index >= St->Length)
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();

  const int64_t Index = St->Index++;
  int64_t Value = 0;
  switch (St->Kind) {
  case 0:
    Value = YonaRuntimeSequenceGet((int64_t *)St->Source, Index);
    break;
  case 1:
    Value = YonaRuntimeByteArrayGet((int64_t *)St->Source, Index);
    break;
  case 2:
    Value = YonaRuntimeIntArrayGet((int64_t *)St->Source, Index);
    break;
  case 3: {
    const double Number = YonaRuntimeFloatArrayGet((double *)St->Source, Index);
    memcpy(&Value, &Number, sizeof(Value));
    break;
  }
  }
  return (int64_t)(intptr_t)YonaRuntimeMakeSome(Value);
}

static int64_t makeIndexedIterator(void *Source, int64_t Length, int Kind) {
  IndexedIteratorState *St =
      (IndexedIteratorState *)YonaRuntimeNativeStateAllocate(
          sizeof(IndexedIteratorState), indexedIterFinalize);
  St->Source = Source;
  St->Index = 0;
  St->Length = Length;
  St->Kind = Kind;
  if (Source)
    YonaRuntimeRetain(Source);
  int64_t *Closure =
      (int64_t *)YonaRuntimeClosureCreate((void *)indexedIterNext, 0, 0, 1);
  YonaRuntimeClosureSetCapture(Closure, 0, (int64_t)(intptr_t)St);
  YonaRuntimeClosureSetHeapMask(Closure, 1);
  return (int64_t)(intptr_t)YonaRuntimeMakeIterator(Closure);
}

int64_t YonaStdIteratorFromSeq(int64_t *Values) {
  return makeIndexedIterator(Values, YonaRuntimeSequenceLength(Values), 0);
}

int64_t YonaStdIteratorFromByteArray(int64_t *Values) {
  return makeIndexedIterator(Values, YonaRuntimeByteArrayLength(Values), 1);
}

int64_t YonaStdIteratorFromIntArray(int64_t *Values) {
  return makeIndexedIterator(Values, YonaRuntimeIntArrayLength(Values), 2);
}

int64_t YonaStdIteratorFromFloatArray(double *Values) {
  return makeIndexedIterator(Values, YonaRuntimeFloatArrayLength(Values), 3);
}

int64_t YonaStdIteratorNextNative(int64_t *Iterator) {
  if (!Iterator || Iterator[0] != 0 || Iterator[1] != 1)
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();
  int64_t *Closure =
      (int64_t *)(intptr_t)Iterator[YONA_ITERATOR_ADT_HEADER_SIZE];
  if (!Closure)
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();
  typedef int64_t (*NextFnT)(int64_t *);
  NextFnT Advance = (NextFnT)(intptr_t)Closure[0];
  return Advance(Closure);
}
