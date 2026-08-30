/*
 * HAMT — Hash Array Mapped Trie
 *
 * Persistent (immutable) hash map with structural sharing.
 * Used for Yona's Dict and Set types.
 *
 * O(1) amortized lookup/insert/delete (max 7 levels for 32-bit hash).
 * 32-way branching at each level, bitmap-compressed to avoid sparse arrays.
 *
 * Node layout (RC-managed via RC_TYPE_DICT):
 *   [datamap: i64] [nodemap: i64] [size: i64] [entries...] [children...]
 *
 *   datamap: bitmap of which 32 slots contain inline key-value entries
 *   nodemap: bitmap of which 32 slots contain child sub-nodes
 *   size:    total entries in this subtree (for O(1) length)
 *
 *   Inline entries: [key0, val0, key1, val1, ...] — popcount(datamap) entries
 *   Child nodes:    [child0, child1, ...] — popcount(nodemap) children (ptrs)
 *
 * Reference: Bagwell (2001) "Ideal Hash Trees", Steindorfer & Vinju (2015)
 * CHAMP
 */

#include "Runtime/Collections/HamtInternal.h"
#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YONA_RC_TYPE_DICT 3
#define YONA_HAMT_BITS 5
#define YONA_HAMT_WIDTH (1 << YONA_HAMT_BITS) /* 32 */
#define YONA_HAMT_MASK (YONA_HAMT_WIDTH - 1)  /* 0x1F */
#define YONA_HAMT_HDR 3                       /* datamap, nodemap, size */

/* Extra bits in the RC type_tag word (bits 16+; unused for HAMT).
 * Bits 0-7 = type tag, 8-15 = pool class. */
/* ===== Hash function (splitmix64) ===== */

static uint64_t hamtHash(int64_t Key) {
  uint64_t H = (uint64_t)Key;
  H = (H ^ (H >> 30)) * 0xbf58476d1ce4e5b9ULL;
  H = (H ^ (H >> 27)) * 0x94d049bb133111ebULL;
  return H ^ (H >> 31);
}

/* ===== Popcount ===== */

static int popcnt(uint64_t X) { return __builtin_popcountll(X); }

/* Index of a bit in the compressed array = popcount of bits below it */
static int hamtIndex(uint64_t Bitmap, uint64_t Bit) {
  return popcnt(Bitmap & (Bit - 1));
}

/* ===== Node allocation ===== */

static void hamtCopyAuxFlags(YonaHamtNode *Dst, YonaHamtNode *Src) {
  if (!Dst || !Src)
    return;
  int64_t *D = ((int64_t *)Dst) - 1;
  int64_t *S = ((int64_t *)Src) - 1;
  *D = (*D & 0xFFFFLL) | (*S & ~0xFFFFLL);
}

/* Transient (unique-owner) check for in-place mutation. */
static int hamtIsUnique(YonaHamtNode *N) {
  if (!N)
    return 0;
  int64_t *Hdr = ((int64_t *)N) - 2;
  return __builtin_expect(__atomic_load_n(&Hdr[0], __ATOMIC_ACQUIRE) == 1, 1);
}

static void hamtRetainSlot(int64_t Flags, int64_t Key, int64_t Val) {
  if ((Flags & YONA_HAMT_FLAG_KEY_HEAP) && Key)
    YonaRuntimeRetain((void *)(intptr_t)Key);
  if ((Flags & YONA_HAMT_FLAG_VALUE_HEAP) && Val)
    YonaRuntimeRetain((void *)(intptr_t)Val);
}

static void hamtReleaseSlot(int64_t Flags, int64_t Key, int64_t Val) {
  if ((Flags & YONA_HAMT_FLAG_KEY_HEAP) && Key)
    YonaRuntimeRelease((void *)(intptr_t)Key);
  if ((Flags & YONA_HAMT_FLAG_VALUE_HEAP) && Val)
    YonaRuntimeRelease((void *)(intptr_t)Val);
}

static YonaHamtNode *hamtAlloc(int DataCount, int NodeCount, int64_t Size) {
  /* At each HAMT level data_count + node_count ≤ 32 by invariant
   * (branching factor); assert to catch caller bugs that would
   * otherwise silently wrap the byte-count multiplication. */
  if (DataCount < 0 || NodeCount < 0 || DataCount > YONA_HAMT_WIDTH ||
      NodeCount > YONA_HAMT_WIDTH) {
    fprintf(stderr, "hamt_alloc: invalid counts data=%d node=%d\n", DataCount,
            NodeCount);
    abort();
  }
  size_t PayloadBytes = (size_t)(DataCount * 2 + NodeCount) * sizeof(int64_t);
  YonaHamtNode *N = (YonaHamtNode *)YonaRuntimeAllocate(
      YONA_RC_TYPE_DICT, sizeof(YonaHamtNode) + PayloadBytes);
  N->Datamap = 0;
  N->Nodemap = 0;
  N->Size = Size;
  return N;
}

static YonaHamtNode *hamtAllocLike(int DataCount, int NodeCount, int64_t Size,
                                   YonaHamtNode *Src) {
  YonaHamtNode *N = hamtAlloc(DataCount, NodeCount, Size);
  if (Src)
    hamtCopyAuxFlags(N, Src);
  return N;
}

static int hamtDataCount(YonaHamtNode *N) {
  return popcnt((uint64_t)N->Datamap);
}
static int hamtNodeCount(YonaHamtNode *N) {
  return popcnt((uint64_t)N->Nodemap);
}

/* Data entries start at payload[0], children after all data */
static int64_t hamtDataKey(YonaHamtNode *N, int Idx) {
  return N->Payload[Idx * 2];
}
static int64_t hamtDataVal(YonaHamtNode *N, int Idx) {
  return N->Payload[Idx * 2 + 1];
}
static YonaHamtNode *hamtChild(YonaHamtNode *N, int Idx) {
  int Dc = hamtDataCount(N);
  return (YonaHamtNode *)(intptr_t)N->Payload[Dc * 2 + Idx];
}

/* ===== Empty ===== */

YonaHamtNode *YonaRuntimeHamtCreate(void) { return hamtAlloc(0, 0, 0); }

/* ===== Lookup ===== */

int64_t YonaRuntimeHamtGet(YonaHamtNode *Node, int64_t Key,
                           int64_t DefaultVal) {
  if (!Node)
    return DefaultVal;
  uint64_t Hash = hamtHash(Key);
  int Shift = 0;

  while (Node) {
    uint64_t Frag = (Hash >> Shift) & YONA_HAMT_MASK;
    uint64_t Bit = (uint64_t)1 << Frag;

    if ((uint64_t)Node->Datamap & Bit) {
      /* Inline data entry */
      int Idx = hamtIndex((uint64_t)Node->Datamap, Bit);
      if (hamtDataKey(Node, Idx) == Key)
        return hamtDataVal(Node, Idx);
      return DefaultVal; /* hash collision slot occupied by different key */
    }
    if ((uint64_t)Node->Nodemap & Bit) {
      /* Child sub-node */
      int Idx = hamtIndex((uint64_t)Node->Nodemap, Bit);
      Node = hamtChild(Node, Idx);
      Shift += YONA_HAMT_BITS;
      continue;
    }
    return DefaultVal; /* slot empty */
  }
  return DefaultVal;
}

int64_t YonaRuntimeHamtContains(YonaHamtNode *Node, int64_t Key) {
  /* Use a sentinel that can't be a real value */
  int64_t Sentinel = (int64_t)0xDEADBEEFCAFEBABEULL;
  return YonaRuntimeHamtGet(Node, Key, Sentinel) != Sentinel ? 1 : 0;
}

/* ===== Insert (persistent) ===== */

static YonaHamtNode *hamtCopyWithData(YonaHamtNode *Old, int InsertIdx,
                                      int64_t Key, int64_t Val) {
  int Dc = hamtDataCount(Old);
  int Nc = hamtNodeCount(Old);
  int64_t Flags = yonaRuntimeHamtFlags(Old);
  YonaHamtNode *N = hamtAllocLike(Dc + 1, Nc, Old->Size + 1, Old);
  N->Datamap = Old->Datamap;
  N->Nodemap = Old->Nodemap;

  /* Copy data entries, inserting new one at insert_idx */
  for (int I = 0; I < InsertIdx; I++) {
    N->Payload[I * 2] = Old->Payload[I * 2];
    N->Payload[I * 2 + 1] = Old->Payload[I * 2 + 1];
    hamtRetainSlot(Flags, N->Payload[I * 2], N->Payload[I * 2 + 1]);
  }
  N->Payload[InsertIdx * 2] = Key;
  N->Payload[InsertIdx * 2 + 1] = Val;
  for (int I = InsertIdx; I < Dc; I++) {
    N->Payload[(I + 1) * 2] = Old->Payload[I * 2];
    N->Payload[(I + 1) * 2 + 1] = Old->Payload[I * 2 + 1];
    hamtRetainSlot(Flags, N->Payload[(I + 1) * 2], N->Payload[(I + 1) * 2 + 1]);
  }

  /* Copy child pointers (rc_inc each) */
  for (int I = 0; I < Nc; I++) {
    int64_t ChildPtr = Old->Payload[Dc * 2 + I];
    N->Payload[(Dc + 1) * 2 + I] = ChildPtr;
    if (ChildPtr)
      YonaRuntimeRetain((void *)(intptr_t)ChildPtr);
  }
  return N;
}

static YonaHamtNode *hamtCopyReplaceData(YonaHamtNode *Old, int DataIdx,
                                         int64_t NewVal) {
  int Dc = hamtDataCount(Old);
  int Nc = hamtNodeCount(Old);
  int64_t Flags = yonaRuntimeHamtFlags(Old);
  YonaHamtNode *N = hamtAllocLike(Dc, Nc, Old->Size, Old);
  N->Datamap = Old->Datamap;
  N->Nodemap = Old->Nodemap;

  /* Copy all data, replacing value at data_idx */
  for (int I = 0; I < Dc; I++) {
    N->Payload[I * 2] = Old->Payload[I * 2];
    hamtRetainSlot(Flags, N->Payload[I * 2], 0);
    if (I == DataIdx) {
      N->Payload[I * 2 + 1] = NewVal;
    } else {
      N->Payload[I * 2 + 1] = Old->Payload[I * 2 + 1];
      hamtRetainSlot(Flags, 0, N->Payload[I * 2 + 1]);
    }
  }
  /* Copy children */
  for (int I = 0; I < Nc; I++) {
    int64_t ChildPtr = Old->Payload[Dc * 2 + I];
    N->Payload[Dc * 2 + I] = ChildPtr;
    if (ChildPtr)
      YonaRuntimeRetain((void *)(intptr_t)ChildPtr);
  }
  return N;
}

static YonaHamtNode *hamtCopyPromoteToNode(YonaHamtNode *Old, int DataIdx,
                                           YonaHamtNode *ChildNode,
                                           uint64_t Bit) {
  int Dc = hamtDataCount(Old);
  int Nc = hamtNodeCount(Old);
  int ChildIdx = hamtIndex((uint64_t)Old->Nodemap | Bit, Bit);
  int64_t Flags = yonaRuntimeHamtFlags(Old);

  YonaHamtNode *N = hamtAllocLike(Dc - 1, Nc + 1, Old->Size + 1, Old);
  N->Datamap = Old->Datamap & ~(int64_t)Bit;
  N->Nodemap = Old->Nodemap | (int64_t)Bit;

  /* Copy data entries, skipping data_idx */
  int Di = 0;
  for (int I = 0; I < Dc; I++) {
    if (I == DataIdx)
      continue;
    N->Payload[Di * 2] = Old->Payload[I * 2];
    N->Payload[Di * 2 + 1] = Old->Payload[I * 2 + 1];
    hamtRetainSlot(Flags, N->Payload[Di * 2], N->Payload[Di * 2 + 1]);
    Di++;
  }

  /* Copy old children + insert new child at child_idx (rc_inc all) */
  int Ci = 0;
  for (int I = 0; I < Nc + 1; I++) {
    int64_t Cp;
    if (I == ChildIdx) {
      Cp = (int64_t)(intptr_t)ChildNode;
      /* child_node is freshly created, rc=1, no inc needed */
    } else {
      Cp = Old->Payload[Dc * 2 + Ci];
      if (Cp)
        YonaRuntimeRetain((void *)(intptr_t)Cp);
      Ci++;
    }
    N->Payload[(Dc - 1) * 2 + I] = Cp;
  }
  return N;
}

static YonaHamtNode *hamtMergeTwo(int64_t Key1, int64_t Val1, uint64_t Hash1,
                                  int64_t Key2, int64_t Val2, uint64_t Hash2,
                                  int Shift, int64_t Flags) {
  if (Shift >= 64) {
    /* Hash collision at max depth: store both in a data node */
    YonaHamtNode *N = hamtAlloc(2, 0, 2);
    yonaRuntimeHamtAddFlags(N, Flags);
    uint64_t Frag = Hash1 & YONA_HAMT_MASK;
    uint64_t Bit = (uint64_t)1 << Frag;
    /* Use two different bits (wrap around) */
    uint64_t Frag2 = (Frag + 1) & YONA_HAMT_MASK;
    uint64_t Bit2 = (uint64_t)1 << Frag2;
    N->Datamap = (int64_t)(Bit | Bit2);
    int Idx1 = hamtIndex(Bit | Bit2, Bit);
    int Idx2 = hamtIndex(Bit | Bit2, Bit2);
    N->Payload[Idx1 * 2] = Key1;
    N->Payload[Idx1 * 2 + 1] = Val1;
    N->Payload[Idx2 * 2] = Key2;
    N->Payload[Idx2 * 2 + 1] = Val2;
    hamtRetainSlot(Flags, Key1, Val1);
    return N;
  }

  uint64_t Frag1 = (Hash1 >> Shift) & YONA_HAMT_MASK;
  uint64_t Frag2 = (Hash2 >> Shift) & YONA_HAMT_MASK;

  if (Frag1 == Frag2) {
    /* Same slot: recurse deeper */
    YonaHamtNode *Child = hamtMergeTwo(Key1, Val1, Hash1, Key2, Val2, Hash2,
                                       Shift + YONA_HAMT_BITS, Flags);
    YonaHamtNode *N = hamtAlloc(0, 1, 2);
    yonaRuntimeHamtAddFlags(N, Flags);
    uint64_t Bit = (uint64_t)1 << Frag1;
    N->Nodemap = (int64_t)Bit;
    N->Payload[0] = (int64_t)(intptr_t)Child;
    return N;
  }

  /* Different slots: both inline */
  uint64_t Bit1 = (uint64_t)1 << Frag1;
  uint64_t Bit2 = (uint64_t)1 << Frag2;
  YonaHamtNode *N = hamtAlloc(2, 0, 2);
  yonaRuntimeHamtAddFlags(N, Flags);
  N->Datamap = (int64_t)(Bit1 | Bit2);
  int Idx1 = hamtIndex(Bit1 | Bit2, Bit1);
  int Idx2 = hamtIndex(Bit1 | Bit2, Bit2);
  N->Payload[Idx1 * 2] = Key1;
  N->Payload[Idx1 * 2 + 1] = Val1;
  N->Payload[Idx2 * 2] = Key2;
  N->Payload[Idx2 * 2 + 1] = Val2;
  hamtRetainSlot(Flags, Key1, Val1);
  return N;
}

/* Forward declaration */
static YonaHamtNode *hamtPutImpl(YonaHamtNode *Node, int64_t Key, int64_t Val,
                                 uint64_t Hash, int Shift);

YonaHamtNode *YonaRuntimeHamtPut(YonaHamtNode *Node, int64_t Key, int64_t Val) {
  if (!Node) {
    YonaHamtNode *N = hamtAlloc(1, 0, 1);
    uint64_t Hash = hamtHash(Key);
    uint64_t Frag = Hash & YONA_HAMT_MASK;
    N->Datamap = (int64_t)((uint64_t)1 << Frag);
    N->Payload[0] = Key;
    N->Payload[1] = Val;
    return N;
  }

  uint64_t Hash = hamtHash(Key);
  YonaHamtNode *Result = hamtPutImpl(Node, Key, Val, Hash, 0);
  if (Result && Result != Node)
    hamtCopyAuxFlags(Result, Node);
  return Result;
}

static YonaHamtNode *hamtPutImpl(YonaHamtNode *Node, int64_t Key, int64_t Val,
                                 uint64_t Hash, int Shift) {
  uint64_t Frag = (Hash >> Shift) & YONA_HAMT_MASK;
  uint64_t Bit = (uint64_t)1 << Frag;
  int Unique = hamtIsUnique(Node);

  if ((uint64_t)Node->Datamap & Bit) {
    /* Slot has inline data */
    int Idx = hamtIndex((uint64_t)Node->Datamap, Bit);
    int64_t ExistingKey = hamtDataKey(Node, Idx);

    if (ExistingKey == Key) {
      /* Same key: replace value */
      if (Unique) {
        int64_t Flags = yonaRuntimeHamtFlags(Node);
        int64_t OldVal = Node->Payload[Idx * 2 + 1];
        Node->Payload[Idx * 2 + 1] = Val;
        if (OldVal != Val)
          hamtReleaseSlot(Flags, 0, OldVal);
        return Node;
      }
      return hamtCopyReplaceData(Node, Idx, Val);
    }

    /* Different key in same slot: promote to sub-node */
    uint64_t ExistingHash = hamtHash(ExistingKey);
    int64_t ExistingVal = hamtDataVal(Node, Idx);
    YonaHamtNode *Child =
        hamtMergeTwo(ExistingKey, ExistingVal, ExistingHash, Key, Val, Hash,
                     Shift + YONA_HAMT_BITS, yonaRuntimeHamtFlags(Node));
    /* Promote requires changing node size (data→child), can't mutate in place
     */
    return hamtCopyPromoteToNode(Node, Idx, Child, Bit);
  }

  if ((uint64_t)Node->Nodemap & Bit) {
    /* Slot has child node: recurse */
    int Idx = hamtIndex((uint64_t)Node->Nodemap, Bit);
    YonaHamtNode *OldChild = hamtChild(Node, Idx);
    int64_t OldChildSize = OldChild ? OldChild->Size : 0;
    YonaHamtNode *NewChild =
        hamtPutImpl(OldChild, Key, Val, Hash, Shift + YONA_HAMT_BITS);
    int64_t SizeDelta = (NewChild ? NewChild->Size : 0) - OldChildSize;

    if (Unique) {
      /* Transient: swap child pointer in place, no allocation */
      int Dc = hamtDataCount(Node);
      if (NewChild != OldChild)
        YonaRuntimeRelease((void *)(intptr_t)OldChild);
      Node->Payload[Dc * 2 + Idx] = (int64_t)(intptr_t)NewChild;
      Node->Size += SizeDelta;
      return Node;
    }

    /* Copy node, replacing child at idx */
    int Dc = hamtDataCount(Node);
    int Nc = hamtNodeCount(Node);
    int64_t Flags = yonaRuntimeHamtFlags(Node);
    YonaHamtNode *N = hamtAllocLike(Dc, Nc, Node->Size + SizeDelta, Node);
    N->Datamap = Node->Datamap;
    N->Nodemap = Node->Nodemap;
    memcpy(N->Payload, Node->Payload, (size_t)(Dc * 2) * sizeof(int64_t));
    for (int I = 0; I < Dc; I++)
      hamtRetainSlot(Flags, N->Payload[I * 2], N->Payload[I * 2 + 1]);
    for (int I = 0; I < Nc; I++) {
      int64_t Cp;
      if (I == Idx) {
        Cp = (int64_t)(intptr_t)NewChild;
      } else {
        Cp = Node->Payload[Dc * 2 + I];
        if (Cp)
          YonaRuntimeRetain((void *)(intptr_t)Cp);
      }
      N->Payload[Dc * 2 + I] = Cp;
    }
    return N;
  }

  /* Slot empty: add inline data — requires growing payload, can't mutate */
  int Idx = hamtIndex((uint64_t)Node->Datamap | Bit, Bit);
  YonaHamtNode *N = hamtCopyWithData(Node, Idx, Key, Val);
  N->Datamap |= (int64_t)Bit;
  return N;
}

/* ===== Size ===== */

int64_t YonaRuntimeHamtSize(YonaHamtNode *Node) {
  return Node ? Node->Size : 0;
}

/* ===== Iteration (for printing and generators) ===== */

typedef void (*HamtIterFn)(int64_t Key, int64_t Val, void *Ctx);

static void hamtIterateImpl(YonaHamtNode *Node, HamtIterFn Fn, void *Ctx) {
  if (!Node)
    return;
  int Dc = hamtDataCount(Node);
  int Nc = hamtNodeCount(Node);
  for (int I = 0; I < Dc; I++)
    Fn(hamtDataKey(Node, I), hamtDataVal(Node, I), Ctx);
  for (int I = 0; I < Nc; I++)
    hamtIterateImpl(hamtChild(Node, I), Fn, Ctx);
}

/* ===== Print ===== */

typedef struct {
  int First;
  int64_t Flags;
  int AsSet;
} PrintContext;

static void hamtPrintSlot(int64_t V, int Heap) {
  if (Heap)
    YonaRuntimePrintHeapValue(V);
  else
    printf("%" PRId64, V);
}

static void hamtPrintEntry(int64_t Key, int64_t Val, void *Ctx) {
  PrintContext *Pc = (PrintContext *)Ctx;
  if (!Pc->First)
    printf(", ");
  hamtPrintSlot(Key, (Pc->Flags & YONA_HAMT_FLAG_KEY_HEAP) != 0);
  if (!Pc->AsSet) {
    printf(": ");
    hamtPrintSlot(Val, (Pc->Flags & YONA_HAMT_FLAG_VALUE_HEAP) != 0);
  }
  Pc->First = 0;
}

void YonaRuntimeHamtPrint(YonaHamtNode *Node) {
  printf("{");
  if (Node) {
    PrintContext Ctx = {1, yonaRuntimeHamtFlags(Node), 0};
    hamtIterateImpl(Node, hamtPrintEntry, &Ctx);
  }
  printf("}");
}

void YonaRuntimeHamtPrintSet(YonaHamtNode *Node) {
  printf("{");
  if (Node) {
    PrintContext Ctx = {1, yonaRuntimeHamtFlags(Node), 1};
    hamtIterateImpl(Node, hamtPrintEntry, &Ctx);
  }
  printf("}");
}

/* ===== Collect keys to seq (for iteration/generators) ===== */

typedef struct {
  int64_t *Seq;
  int64_t Idx;
  int KeysAreHeap;
} CollectContext;

static void hamtCollectKey(int64_t Key, int64_t Val, void *Ctx) {
  (void)Val;
  CollectContext *Cc = (CollectContext *)Ctx;
  if (Cc->KeysAreHeap && Key)
    YonaRuntimeRetain((void *)(intptr_t)Key);
  YonaRuntimeSequenceSet(Cc->Seq, Cc->Idx, Key);
  Cc->Idx++;
}

int64_t *YonaRuntimeHamtKeys(YonaHamtNode *Node) {
  int64_t Sz = YonaRuntimeHamtSize(Node);
  int64_t *Seq = YonaRuntimeSequenceAllocate(Sz);
  const int KeysAreHeap =
      Node && (yonaRuntimeHamtFlags(Node) & YONA_HAMT_FLAG_KEY_HEAP);
  YonaRuntimeSequenceSetHeap(Seq, KeysAreHeap);
  if (Sz > 0) {
    CollectContext Ctx = {Seq, 0, KeysAreHeap};
    hamtIterateImpl(Node, hamtCollectKey, &Ctx);
  }
  return Seq;
}

/* ===== RC destructor support ===== */
/* Called from YonaRuntimeRelease when a HAMT node's refcount hits 0.
 * rc_dec heap keys/values per aux flags, then child sub-nodes. */

void YonaRuntimeHamtDestroyChildren(void *NodePtr) {
  YonaHamtNode *Node = (YonaHamtNode *)NodePtr;
  if (!Node)
    return;
  int64_t Flags = yonaRuntimeHamtFlags(Node);
  int Dc = hamtDataCount(Node);
  int Nc = hamtNodeCount(Node);
  for (int I = 0; I < Dc; I++)
    hamtReleaseSlot(Flags, hamtDataKey(Node, I), hamtDataVal(Node, I));
  for (int I = 0; I < Nc; I++) {
    void *Child = (void *)(intptr_t)Node->Payload[Dc * 2 + I];
    if (Child)
      YonaRuntimeRelease(Child);
  }
}

void YonaRuntimeHamtStampAuxiliaryFlags(void *NodePtr, int64_t Flags) {
  YonaHamtNode *Node = (YonaHamtNode *)NodePtr;
  if (!Node || !Flags)
    return;
  yonaRuntimeHamtAddFlags(Node, Flags);
  int Nc = hamtNodeCount(Node);
  for (int I = 0; I < Nc; I++)
    YonaRuntimeHamtStampAuxiliaryFlags(hamtChild(Node, I), Flags);
}
