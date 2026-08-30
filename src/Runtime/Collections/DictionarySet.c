#include "Runtime/Collections/HamtInternal.h"
#include "Runtime/Core/Internal.h"
#include "yona/Runtime/Collections/Dictionary.h"
#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Collections/Set.h"
#include "yona/Runtime/Core/Api.h"

#include <stdint.h>
#include <stdio.h>

int64_t *YonaRuntimeDictionaryAllocate(int64_t CapacityHint) {
  (void)CapacityHint;
  return (int64_t *)YonaRuntimeHamtCreate();
}

void YonaRuntimeDictionarySetHeap(int64_t *Dictionary, int64_t KeysAreHeap,
                                  int64_t ValuesAreHeap) {
  if (!Dictionary)
    return;
  int64_t Flags = 0;
  if (KeysAreHeap)
    Flags |= YONA_HAMT_FLAG_KEY_HEAP;
  if (ValuesAreHeap)
    Flags |= YONA_HAMT_FLAG_VALUE_HEAP;
  YonaRuntimeHamtStampAuxiliaryFlags(Dictionary, Flags);
}

int64_t *YonaRuntimeDictionaryPut(int64_t *Dictionary, int64_t Key,
                                  int64_t Value) {
  int64_t *Result =
      (int64_t *)YonaRuntimeHamtPut((YonaHamtNode *)Dictionary, Key, Value);
  if (Dictionary && Result != Dictionary)
    YonaRuntimeRelease(Dictionary);
  return Result;
}

int64_t YonaRuntimeDictionaryGet(int64_t *Dictionary, int64_t Key,
                                 int64_t DefaultValue) {
  return YonaRuntimeHamtGet((YonaHamtNode *)Dictionary, Key, DefaultValue);
}

int64_t YonaRuntimeDictionarySize(int64_t *Dictionary) {
  return YonaRuntimeHamtSize((YonaHamtNode *)Dictionary);
}

int64_t YonaRuntimeDictionaryContains(int64_t *Dictionary, int64_t Key) {
  return YonaRuntimeHamtContains((YonaHamtNode *)Dictionary, Key);
}

int64_t *YonaRuntimeDictionaryKeys(int64_t *Dictionary) {
  return YonaRuntimeHamtKeys((YonaHamtNode *)Dictionary);
}

void YonaRuntimePrintDictionary(int64_t *Dictionary) {
  YonaRuntimeHamtPrint((YonaHamtNode *)Dictionary);
}

int64_t *YonaRuntimeSetAllocate(int64_t CapacityHint) {
  (void)CapacityHint;
  YonaHamtNode *Set = YonaRuntimeHamtCreate();
  yonaRuntimeHamtAddFlags(Set, YONA_HAMT_FLAG_IS_SET);
  return (int64_t *)Set;
}

void YonaRuntimeSetSetHeap(int64_t *Set, int64_t ElementsAreHeap) {
  if (!Set)
    return;
  int64_t Flags = YONA_HAMT_FLAG_IS_SET;
  if (ElementsAreHeap)
    Flags |= YONA_HAMT_FLAG_KEY_HEAP;
  YonaRuntimeHamtStampAuxiliaryFlags(Set, Flags);
}

int64_t *YonaRuntimeSetInsert(int64_t *Set, int64_t Element) {
  int64_t *Result =
      (int64_t *)YonaRuntimeHamtPut((YonaHamtNode *)Set, Element, 1);
  yonaRuntimeHamtAddFlags((YonaHamtNode *)Result, YONA_HAMT_FLAG_IS_SET);
  if (Set && Result != Set)
    YonaRuntimeRelease(Set);
  return Result;
}

int64_t YonaRuntimeSetContains(int64_t *Set, int64_t Element) {
  return YonaRuntimeHamtContains((YonaHamtNode *)Set, Element);
}

int64_t YonaRuntimeSetSize(int64_t *Set) {
  return YonaRuntimeHamtSize((YonaHamtNode *)Set);
}

int64_t *YonaRuntimeSetElements(int64_t *Set) {
  if (!Set)
    return YonaRuntimeSequenceAllocate(0);
  return YonaRuntimeHamtKeys((YonaHamtNode *)Set);
}

static int setKeysAreHeap(int64_t *Set) {
  return (yonaRuntimeHamtFlags((YonaHamtNode *)Set) &
          YONA_HAMT_FLAG_KEY_HEAP) != 0;
}

static YonaHamtNode *ensureSet(int64_t *Set) {
  if (Set)
    return (YonaHamtNode *)Set;
  YonaHamtNode *Result = YonaRuntimeHamtCreate();
  yonaRuntimeHamtAddFlags(Result, YONA_HAMT_FLAG_IS_SET);
  return Result;
}

static YonaHamtNode *putConsuming(YonaHamtNode *Set, int64_t Element) {
  YonaHamtNode *Result = YonaRuntimeHamtPut(Set, Element, 1);
  if (Set && Result != Set)
    YonaRuntimeRelease(Set);
  return Result;
}

int64_t *YonaRuntimeSetUnion(int64_t *Left, int64_t *Right) {
  YonaHamtNode *Result = ensureSet(Left);
  int64_t ResultFlags = YONA_HAMT_FLAG_IS_SET;
  if (setKeysAreHeap(Left) || setKeysAreHeap(Right))
    ResultFlags |= YONA_HAMT_FLAG_KEY_HEAP;
  yonaRuntimeHamtAddFlags(Result, ResultFlags);

  int64_t *Elements = YonaRuntimeSetElements(Right);
  const int ResultKeysAreHeap = (ResultFlags & YONA_HAMT_FLAG_KEY_HEAP) != 0;
  for (int64_t Index = 0; Index < YonaRuntimeSequenceLength(Elements);
       Index++) {
    int64_t Element = YonaRuntimeSequenceGet(Elements, Index);
    if (YonaRuntimeHamtContains(Result, Element))
      continue;
    if (ResultKeysAreHeap && Element)
      YonaRuntimeRetain((void *)(intptr_t)Element);
    Result = putConsuming(Result, Element);
  }
  YonaRuntimeRelease(Elements);
  return (int64_t *)Result;
}

int64_t *YonaRuntimeSetIntersection(int64_t *Left, int64_t *Right) {
  YonaHamtNode *Result = YonaRuntimeHamtCreate();
  int64_t ResultFlags = YONA_HAMT_FLAG_IS_SET;
  if (setKeysAreHeap(Left))
    ResultFlags |= YONA_HAMT_FLAG_KEY_HEAP;
  yonaRuntimeHamtAddFlags(Result, ResultFlags);

  int64_t *Elements = YonaRuntimeSetElements(Left);
  YonaRuntimeRelease(Left);
  for (int64_t Index = 0; Index < YonaRuntimeSequenceLength(Elements);
       Index++) {
    int64_t Element = YonaRuntimeSequenceGet(Elements, Index);
    if (!YonaRuntimeSetContains(Right, Element))
      continue;
    if ((ResultFlags & YONA_HAMT_FLAG_KEY_HEAP) && Element)
      YonaRuntimeRetain((void *)(intptr_t)Element);
    Result = putConsuming(Result, Element);
  }
  YonaRuntimeRelease(Elements);
  return (int64_t *)Result;
}

int64_t *YonaRuntimeSetDifference(int64_t *Left, int64_t *Right) {
  YonaHamtNode *Result = YonaRuntimeHamtCreate();
  int64_t ResultFlags = YONA_HAMT_FLAG_IS_SET;
  if (setKeysAreHeap(Left))
    ResultFlags |= YONA_HAMT_FLAG_KEY_HEAP;
  yonaRuntimeHamtAddFlags(Result, ResultFlags);

  int64_t *Elements = YonaRuntimeSetElements(Left);
  YonaRuntimeRelease(Left);
  for (int64_t Index = 0; Index < YonaRuntimeSequenceLength(Elements);
       Index++) {
    int64_t Element = YonaRuntimeSequenceGet(Elements, Index);
    if (YonaRuntimeSetContains(Right, Element))
      continue;
    if ((ResultFlags & YONA_HAMT_FLAG_KEY_HEAP) && Element)
      YonaRuntimeRetain((void *)(intptr_t)Element);
    Result = putConsuming(Result, Element);
  }
  YonaRuntimeRelease(Elements);
  return (int64_t *)Result;
}

void YonaRuntimePrintSet(int64_t *Set) {
  if (!Set) {
    printf("{}");
    return;
  }
  YonaRuntimeHamtPrintSet((YonaHamtNode *)Set);
}

#define YONA_HAMT_ITERATOR_MAXIMUM_DEPTH 14

typedef struct {
  YonaHamtNode *Node;
  int DataIndex;
  int ChildIndex;
  int DataCount;
  int NodeCount;
} HamtStackFrame;

typedef struct {
  void (*Finalize)(void *);
  HamtStackFrame Stack[YONA_HAMT_ITERATOR_MAXIMUM_DEPTH];
  int Depth;
  int64_t *Root;
} HamtIteratorState;

static void finalizeHamtIterator(void *RawState) {
  HamtIteratorState *State = (HamtIteratorState *)RawState;
  if (State->Root)
    YonaRuntimeRelease(State->Root);
}

static void pushHamtIteratorNode(HamtIteratorState *State, YonaHamtNode *Node) {
  if (!Node || State->Depth >= YONA_HAMT_ITERATOR_MAXIMUM_DEPTH)
    return;
  HamtStackFrame *Frame = &State->Stack[State->Depth++];
  Frame->Node = Node;
  Frame->DataIndex = 0;
  Frame->ChildIndex = 0;
  Frame->DataCount = __builtin_popcountll((uint64_t)Node->Datamap);
  Frame->NodeCount = __builtin_popcountll((uint64_t)Node->Nodemap);
}

static int advanceHamtIterator(HamtIteratorState *State, int64_t *OutputKey,
                               int64_t *OutputValue) {
  while (State->Depth > 0) {
    HamtStackFrame *Frame = &State->Stack[State->Depth - 1];
    if (Frame->DataIndex < Frame->DataCount) {
      *OutputKey = Frame->Node->Payload[Frame->DataIndex * 2];
      *OutputValue = Frame->Node->Payload[Frame->DataIndex * 2 + 1];
      Frame->DataIndex++;
      return 1;
    }
    if (Frame->ChildIndex < Frame->NodeCount) {
      YonaHamtNode *Child =
          (YonaHamtNode *)(intptr_t)
              Frame->Node->Payload[Frame->DataCount * 2 + Frame->ChildIndex];
      Frame->ChildIndex++;
      pushHamtIteratorNode(State, Child);
      continue;
    }
    State->Depth--;
  }
  return 0;
}

static int64_t advanceDictionaryEntries(int64_t *Environment) {
  HamtIteratorState *State = (HamtIteratorState *)(intptr_t)Environment[6];
  int64_t Key;
  int64_t Value;
  if (!advanceHamtIterator(State, &Key, &Value))
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();

  const int64_t Flags = yonaRuntimeHamtFlags((YonaHamtNode *)State->Root);
  int64_t HeapMask = 0;
  if ((Flags & YONA_HAMT_FLAG_KEY_HEAP) && Key) {
    YonaRuntimeRetain((void *)(intptr_t)Key);
    HeapMask |= 1;
  }
  if ((Flags & YONA_HAMT_FLAG_VALUE_HEAP) && Value) {
    YonaRuntimeRetain((void *)(intptr_t)Value);
    HeapMask |= 2;
  }
  int64_t *Tuple = (int64_t *)YonaRuntimeTupleAllocate(2);
  YonaRuntimeTupleSet(Tuple, 0, Key);
  YonaRuntimeTupleSet(Tuple, 1, Value);
  YonaRuntimeTupleSetHeapMask(Tuple, HeapMask);
  return (int64_t)(intptr_t)YonaRuntimeMakeSome((int64_t)(intptr_t)Tuple);
}

static int64_t advanceDictionaryKeys(int64_t *Environment) {
  HamtIteratorState *State = (HamtIteratorState *)(intptr_t)Environment[6];
  int64_t Key;
  int64_t IgnoredValue;
  if (!advanceHamtIterator(State, &Key, &IgnoredValue))
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();
  if ((yonaRuntimeHamtFlags((YonaHamtNode *)State->Root) &
       YONA_HAMT_FLAG_KEY_HEAP) &&
      Key)
    YonaRuntimeRetain((void *)(intptr_t)Key);
  return (int64_t)(intptr_t)YonaRuntimeMakeSome(Key);
}

static int64_t advanceDictionaryValues(int64_t *Environment) {
  HamtIteratorState *State = (HamtIteratorState *)(intptr_t)Environment[6];
  int64_t IgnoredKey;
  int64_t Value;
  if (!advanceHamtIterator(State, &IgnoredKey, &Value))
    return (int64_t)(intptr_t)YonaRuntimeMakeNone();
  if ((yonaRuntimeHamtFlags((YonaHamtNode *)State->Root) &
       YONA_HAMT_FLAG_VALUE_HEAP) &&
      Value)
    YonaRuntimeRetain((void *)(intptr_t)Value);
  return (int64_t)(intptr_t)YonaRuntimeMakeSome(Value);
}

static int64_t advanceSetElements(int64_t *Environment) {
  return advanceDictionaryKeys(Environment);
}

static int64_t makeHamtIterator(int64_t *Collection, void *AdvanceFunction) {
  HamtIteratorState *State =
      (HamtIteratorState *)YonaRuntimeNativeStateAllocate(
          sizeof(HamtIteratorState), finalizeHamtIterator);
  State->Depth = 0;
  State->Root = Collection;
  if (Collection)
    YonaRuntimeRetain(Collection);
  pushHamtIteratorNode(State, (YonaHamtNode *)Collection);

  int64_t *Closure =
      (int64_t *)YonaRuntimeClosureCreate(AdvanceFunction, 0, 0, 1);
  YonaRuntimeClosureSetCapture(Closure, 0, (int64_t)(intptr_t)State);
  YonaRuntimeClosureSetHeapMask(Closure, 1);
  return (int64_t)(intptr_t)YonaRuntimeMakeIterator(Closure);
}

int64_t YonaStdDictEntries(int64_t *Dictionary) {
  return makeHamtIterator(Dictionary, (void *)advanceDictionaryEntries);
}

int64_t YonaStdDictKeysIter(int64_t *Dictionary) {
  return makeHamtIterator(Dictionary, (void *)advanceDictionaryKeys);
}

int64_t YonaStdDictValues(int64_t *Dictionary) {
  return makeHamtIterator(Dictionary, (void *)advanceDictionaryValues);
}

int64_t YonaStdSetIterator(int64_t *Set) {
  return makeHamtIterator(Set, (void *)advanceSetElements);
}

int64_t YonaStdDictForEach(int64_t *Function, int64_t *Dictionary) {
  HamtIteratorState State = {0};
  State.Root = Dictionary;
  pushHamtIteratorNode(&State, (YonaHamtNode *)Dictionary);
  int64_t Key;
  int64_t Value;
  typedef int64_t (*CallbackFunction)(int64_t *, int64_t, int64_t);
  CallbackFunction Callback = (CallbackFunction)(intptr_t)Function[0];
  while (advanceHamtIterator(&State, &Key, &Value))
    Callback(Function, Key, Value);
  return 0;
}

int64_t YonaStdSetForEach(int64_t *Function, int64_t *Set) {
  HamtIteratorState State = {0};
  State.Root = Set;
  pushHamtIteratorNode(&State, (YonaHamtNode *)Set);
  int64_t Key;
  int64_t IgnoredValue;
  typedef int64_t (*CallbackFunction)(int64_t *, int64_t);
  CallbackFunction Callback = (CallbackFunction)(intptr_t)Function[0];
  while (advanceHamtIterator(&State, &Key, &IgnoredValue))
    Callback(Function, Key);
  return 0;
}
