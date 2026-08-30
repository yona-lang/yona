/*
 * Seq — persistent immutable sequence.
 *
 * Two representations:
 *   Small (≤32 elements): flat array [length, heap_flag, elem0, ...]
 * (RC_TYPE_SEQ) Large (>32 elements):  RBT with head chain + optional trie +
 * tail buffer
 *
 * The head chain is a linked list of 32-element buffers for O(1)
 * cons/head/tail. The trie is a 32-way radix-balanced trie for O(log32 n)
 * indexed access on snoc-built elements. The tail buffer absorbs snoc for O(1)
 * amortized append.
 *
 * Time complexities:
 *   cons (prepend):  O(1) amortized
 *   head:            O(1)
 *   tail:            O(1) amortized
 *   snoc (append):   O(1) amortized
 *   get(i):          O(1) small / O(n/32) head chain / O(log32 n) trie
 *   length:          O(1)
 *   concat:          O(n)
 */

#include "yona/Runtime/Collections/Sequence.h"

#include "yona/Runtime/Core/Api.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Branch prediction hints — most seq operations take the fast path. */
#define YONA_LIKELY(x) __builtin_expect(!!(x), 1)
#define YONA_UNLIKELY(x) __builtin_expect(!!(x), 0)

#define YONA_RC_TYPE_SEQ 1
#define YONA_RC_TYPE_RBT 12
#define YONA_RC_TYPE_RBT_NODE 13
#define YONA_RC_TYPE_RBT_LEAF 14
#define YONA_RC_TYPE_RBT_CHUNK 15
#define YONA_SEQUENCE_BRANCHING_FACTOR 32
#define YONA_SEQUENCE_BRANCH_BITS 5
#define YONA_SEQUENCE_BRANCH_MASK (YONA_SEQUENCE_BRANCHING_FACTOR - 1)
#define YONA_RC_ARENA_SENTINEL INT64_MAX
#define YONA_SEQ_HDR_SIZE 2

/* Flat seq layout: [count, flags, elem0, elem1, ...]
 * flags encodes both heap_flag and offset for O(1) tail:
 *   bits 0-31:  heap_flag (1 if elements are heap pointers)
 *   bits 32-63: offset (index of first valid element in elems[])
 * Access: seq[SEQ_HDR_SIZE + FLAT_OFF(seq) + index] */
#define YONA_FLAT_OFF(seq) ((int)((uint64_t)(seq)[1] >> 32))
#define YONA_FLAT_HF(seq) ((int)((seq)[1] & 0xFFFFFFFF))
#define YONA_FLAT_SET_OFF_HF(seq, off, hf)                                     \
  ((seq)[1] = ((int64_t)(off) << 32) | ((hf) & 0xFFFFFFFF))

/* ===== Types ===== */

typedef struct {
  int64_t Children[YONA_SEQUENCE_BRANCHING_FACTOR];
} RbtNode;
typedef struct {
  int64_t HeapFlag;
  int64_t Elems[YONA_SEQUENCE_BRANCHING_FACTOR];
} RbtLeaf;

typedef struct RbtChunk {
  int64_t Offset;
  int64_t Count;
  int64_t Elems[YONA_SEQUENCE_BRANCHING_FACTOR];
  struct RbtChunk *Next; /* RC-managed */
} RbtChunk;

/* RBT root — length at offset 0 for codegen's inline seq[0] reads.
 * back_off tracks how many trie elements have been consumed from the
 * front (by tail draining into head_buf), avoiding O(n) trie rebuilds. */
typedef struct {
  int64_t Length;
  int64_t HeapFlag;
  int64_t HeadOff;
  int64_t HeadCnt;
  int64_t HeadBuf[YONA_SEQUENCE_BRANCHING_FACTOR];
  RbtChunk *HeadNext;
  int64_t HeadChainLen;
  int64_t TailCnt;
  int64_t TailBuf[YONA_SEQUENCE_BRANCHING_FACTOR];
  int64_t BackShift;
  void *BackRoot;
  int64_t BackSize; /* total elements ever pushed into trie */
  int64_t BackOff;  /* consumed from front of trie (logical offset) */
} Rbt;

/* Active trie elements = back_size - back_off */
static inline int64_t trieActive(Rbt *R) { return R->BackSize - R->BackOff; }

/* ===== Low-level helpers ===== */

static inline __attribute__((always_inline)) int isRbt(int64_t *Seq) {
  if (!Seq)
    return 0;
  return (((int64_t *)Seq)[-1] & 0xFF) == YONA_RC_TYPE_RBT;
}

static inline __attribute__((always_inline)) int isUnique(void *Ptr) {
  if (!Ptr)
    return 0;
  return __atomic_load_n(((int64_t *)Ptr) - 2, __ATOMIC_ACQUIRE) == 1;
}

static inline void retainHeapValue(int64_t HeapFlag, int64_t Value) {
  if (HeapFlag && Value)
    YonaRuntimeRetain((void *)(intptr_t)Value);
}

static inline void releaseHeapValue(int64_t HeapFlag, int64_t Value) {
  if (HeapFlag && Value)
    YonaRuntimeRelease((void *)(intptr_t)Value);
}

static void retainHeapRange(int64_t HeapFlag, const int64_t *Values,
                            int64_t Count) {
  if (!HeapFlag)
    return;
  for (int64_t I = 0; I < Count; ++I)
    retainHeapValue(1, Values[I]);
}

/* RBT roots own the values stored directly in their head/tail buffers and,
 * by current destructor contract, the values reachable through head chunks.
 * A shallow root clone therefore needs independent element references even
 * though the chunk nodes themselves are shared. Trie leaves are RC-owned by
 * the shared trie and must not be retained here. */
static void retainRbtDirectValues(const Rbt *R) {
  if (!R->HeapFlag)
    return;
  retainHeapRange(1, R->HeadBuf + R->HeadOff, R->HeadCnt);
  retainHeapRange(1, R->TailBuf, R->TailCnt);
  for (const RbtChunk *Chunk = R->HeadNext; Chunk; Chunk = Chunk->Next)
    retainHeapRange(1, Chunk->Elems + Chunk->Offset, Chunk->Count);
}

static void retainChunkValues(int64_t HeapFlag, const RbtChunk *Chunk) {
  if (!HeapFlag)
    return;
  for (; Chunk; Chunk = Chunk->Next)
    retainHeapRange(1, Chunk->Elems + Chunk->Offset, Chunk->Count);
}

static RbtNode *nodeAlloc(void) {
  RbtNode *N =
      (RbtNode *)YonaRuntimeAllocate(YONA_RC_TYPE_RBT_NODE, sizeof(RbtNode));
  memset(N, 0, sizeof(RbtNode));
  return N;
}

static RbtLeaf *leafAlloc(int64_t Hf) {
  RbtLeaf *L =
      (RbtLeaf *)YonaRuntimeAllocate(YONA_RC_TYPE_RBT_LEAF, sizeof(RbtLeaf));
  L->HeapFlag = Hf;
  memset(L->Elems, 0, sizeof(L->Elems));
  return L;
}

static RbtChunk *chunkAlloc(void) {
  RbtChunk *C =
      (RbtChunk *)YonaRuntimeAllocate(YONA_RC_TYPE_RBT_CHUNK, sizeof(RbtChunk));
  C->Offset = 0;
  C->Count = 0;
  C->Next = NULL;
  return C;
}

/* Fast rbt allocation for cons path — zeroes only tail/trie fields. */
static Rbt *rbtAllocCons(void) {
  Rbt *R = (Rbt *)YonaRuntimeAllocate(YONA_RC_TYPE_RBT, sizeof(Rbt));
  R->HeadNext = NULL;
  R->HeadChainLen = 0;
  R->TailCnt = 0;
  R->BackShift = 0;
  R->BackRoot = NULL;
  R->BackSize = 0;
  R->BackOff = 0;
  return R;
}

static Rbt *rbtAllocZeroed(void) {
  Rbt *R = (Rbt *)YonaRuntimeAllocate(YONA_RC_TYPE_RBT, sizeof(Rbt));
  memset(R, 0, sizeof(Rbt));
  return R;
}

/* Clone rbt header (shallow: rc_inc trie root + head chain). */
static Rbt *rbtClone(Rbt *Src) {
  Rbt *R = (Rbt *)YonaRuntimeAllocate(YONA_RC_TYPE_RBT, sizeof(Rbt));
  memcpy(R, Src, sizeof(Rbt));
  if (R->BackRoot)
    YonaRuntimeRetain(R->BackRoot);
  if (R->HeadNext)
    YonaRuntimeRetain(R->HeadNext);
  retainRbtDirectValues(R);
  return R;
}

/* Copy fields from src rbt into a freshly allocated rbt (for non-unique paths).
 */
static Rbt *rbtCopyBody(Rbt *Src) {
  Rbt *Nr = (Rbt *)YonaRuntimeAllocate(YONA_RC_TYPE_RBT, sizeof(Rbt));
  Nr->HeapFlag = Src->HeapFlag;
  Nr->TailCnt = Src->TailCnt;
  if (Src->TailCnt)
    memcpy(Nr->TailBuf, Src->TailBuf, (size_t)Src->TailCnt * sizeof(int64_t));
  Nr->BackShift = Src->BackShift;
  Nr->BackRoot = Src->BackRoot;
  if (Nr->BackRoot)
    YonaRuntimeRetain(Nr->BackRoot);
  Nr->BackSize = Src->BackSize;
  Nr->BackOff = Src->BackOff;
  retainHeapRange(Src->HeapFlag, Nr->TailBuf, Nr->TailCnt);
  return Nr;
}

static RbtNode *nodeCopy(RbtNode *Src) {
  RbtNode *N = nodeAlloc();
  memcpy(N->Children, Src->Children, sizeof(N->Children));
  for (int I = 0; I < YONA_SEQUENCE_BRANCHING_FACTOR; I++)
    if (N->Children[I])
      YonaRuntimeRetain((void *)(intptr_t)N->Children[I]);
  return N;
}

/* ===== Trie operations (right-side only) ===== */

static int64_t trieGet(void *Node, int64_t Shift, int64_t Index) {
  while (Shift > 0) {
    Node = (void *)(intptr_t)((RbtNode *)Node)
               ->Children[(Index >> Shift) & YONA_SEQUENCE_BRANCH_MASK];
    Shift -= YONA_SEQUENCE_BRANCH_BITS;
  }
  return ((RbtLeaf *)Node)->Elems[Index & YONA_SEQUENCE_BRANCH_MASK];
}

static void *triePushLeaf(void *Node, int64_t Shift, int64_t TrieIdx,
                          RbtLeaf *Leaf) {
  if (Shift == 0)
    return Leaf;
  int Slot = (int)((TrieIdx >> Shift) & YONA_SEQUENCE_BRANCH_MASK);
  RbtNode *N = isUnique(Node) ? (RbtNode *)Node : nodeCopy((RbtNode *)Node);
  void *Child = (void *)(intptr_t)N->Children[Slot];
  if (Shift == YONA_SEQUENCE_BRANCH_BITS) {
    N->Children[Slot] = (int64_t)(intptr_t)Leaf;
  } else if (!Child) {
    RbtNode *Path = nodeAlloc();
    void *Cur = Path;
    for (int64_t S = Shift - YONA_SEQUENCE_BRANCH_BITS;
         S > YONA_SEQUENCE_BRANCH_BITS; S -= YONA_SEQUENCE_BRANCH_BITS) {
      RbtNode *Next = nodeAlloc();
      ((RbtNode *)Cur)->Children[(TrieIdx >> S) & YONA_SEQUENCE_BRANCH_MASK] =
          (int64_t)(intptr_t)Next;
      Cur = Next;
    }
    ((RbtNode *)Cur)
        ->Children[(TrieIdx >> YONA_SEQUENCE_BRANCH_BITS) &
                   YONA_SEQUENCE_BRANCH_MASK] = (int64_t)(intptr_t)Leaf;
    N->Children[Slot] = (int64_t)(intptr_t)Path;
  } else {
    void *NewChild =
        triePushLeaf(Child, Shift - YONA_SEQUENCE_BRANCH_BITS, TrieIdx, Leaf);
    if (NewChild != Child && !isUnique(Node))
      YonaRuntimeRelease(Child);
    N->Children[Slot] = (int64_t)(intptr_t)NewChild;
  }
  return N;
}

static void triePushBuf(Rbt *R, int64_t *Buf) {
  RbtLeaf *Leaf = leafAlloc(R->HeapFlag);
  memcpy(Leaf->Elems, Buf, YONA_SEQUENCE_BRANCHING_FACTOR * sizeof(int64_t));
  if (!R->BackRoot) {
    R->BackRoot = Leaf;
    R->BackShift = 0;
  } else {
    int64_t Cap = 1;
    for (int64_t S = R->BackShift; S >= 0; S -= YONA_SEQUENCE_BRANCH_BITS)
      Cap *= YONA_SEQUENCE_BRANCHING_FACTOR;
    if (R->BackSize >= Cap) {
      RbtNode *Grown = nodeAlloc();
      YonaRuntimeRetain(R->BackRoot);
      Grown->Children[0] = (int64_t)(intptr_t)R->BackRoot;
      R->BackRoot = triePushLeaf(
          Grown, R->BackShift + YONA_SEQUENCE_BRANCH_BITS, R->BackSize, Leaf);
      R->BackShift += YONA_SEQUENCE_BRANCH_BITS;
    } else {
      R->BackRoot = triePushLeaf(R->BackRoot, R->BackShift, R->BackSize, Leaf);
    }
  }
  R->BackSize += YONA_SEQUENCE_BRANCHING_FACTOR;
}

/* ===== Head chain helpers ===== */

static int64_t chainGet(RbtChunk *Chunk, int64_t Index) {
  while (Chunk) {
    if (Index < Chunk->Count)
      return Chunk->Elems[Chunk->Offset + Index];
    Index -= Chunk->Count;
    Chunk = Chunk->Next;
  }
  return 0;
}

/* ===== Public API ===== */

int64_t *YonaRuntimeSequenceAllocate(int64_t Count) {
  /* Reject negatives and sizes that would overflow the byte-count
   * multiplication below. The ceiling is defensive — real programs
   * hit OOM long before this, but silent wraparound is a heap-overflow
   * footgun so we trap instead. */
  if (YONA_UNLIKELY(Count < 0 ||
                    Count > (int64_t)((SIZE_MAX / sizeof(int64_t)) -
                                      YONA_SEQ_HDR_SIZE))) {
    fprintf(stderr, "YonaRuntimeSequenceAllocate: invalid count %lld\n",
            (long long)Count);
    abort();
  }
  int64_t *Seq = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_SEQ, (YONA_SEQ_HDR_SIZE + Count) * sizeof(int64_t));
  Seq[0] = Count;
  Seq[1] = 0;
  return Seq;
}

void YonaRuntimeSequenceSetHeap(int64_t *Sequence, int64_t IsHeap) {
  if (isRbt(Sequence))
    ((Rbt *)Sequence)->HeapFlag = IsHeap;
  else
    YONA_FLAT_SET_OFF_HF(Sequence, YONA_FLAT_OFF(Sequence), (int)IsHeap);
}

int64_t YonaRuntimeSequenceLength(int64_t *Seq) { return Seq[0]; }

int64_t YonaRuntimeSequenceIsEmpty(int64_t *Seq) {
  if (!Seq)
    return 1;
  return Seq[0] == 0;
}

int64_t YonaRuntimeSequenceGet(int64_t *Seq, int64_t Index) {
  if (YONA_UNLIKELY(!Seq || Index < 0 || Index >= Seq[0])) {
    fprintf(stderr,
            "YonaRuntimeSequenceGet: index %lld out of range (len=%lld)\n",
            (long long)Index, (long long)(Seq ? Seq[0] : 0));
    abort();
  }
  if (YONA_LIKELY(!isRbt(Seq)))
    return Seq[YONA_SEQ_HDR_SIZE + YONA_FLAT_OFF(Seq) + Index];
  Rbt *R = (Rbt *)Seq;
  if (YONA_LIKELY(Index < R->HeadCnt))
    return R->HeadBuf[R->HeadOff + Index];
  Index -= R->HeadCnt;
  if (Index < R->HeadChainLen)
    return chainGet(R->HeadNext, Index);
  Index -= R->HeadChainLen;
  int64_t Ta = trieActive(R);
  if (Index < Ta)
    return trieGet(R->BackRoot, R->BackShift, R->BackOff + Index);
  Index -= Ta;
  return R->TailBuf[Index];
}

/* Generic value-return ABI: callers own returned heap values. The primitive
 * seq_get above intentionally remains borrowed for internal hot paths. */
int64_t YonaRuntimeSequenceGetOwned(int64_t *Seq, int64_t Index) {
  int64_t Value = YonaRuntimeSequenceGet(Seq, Index);
  const int64_t HeapFlag =
      isRbt(Seq) ? ((Rbt *)Seq)->HeapFlag : YONA_FLAT_HF(Seq);
  retainHeapValue(HeapFlag, Value);
  return Value;
}

void YonaRuntimeSequenceSet(int64_t *Seq, int64_t Index, int64_t Value) {
  Seq[YONA_SEQ_HDR_SIZE + Index] = Value;
}

int64_t YonaRuntimeSequenceHead(int64_t *Seq) {
  if (YONA_UNLIKELY(isRbt(Seq)))
    return ((Rbt *)Seq)->HeadBuf[((Rbt *)Seq)->HeadOff];
  return Seq[YONA_SEQ_HDR_SIZE + YONA_FLAT_OFF(Seq)];
}

/* ===== Flat-to-RBT promotion ===== */

static Rbt *flatToRbtForCons(int64_t *Flat, int64_t Elem) {
  int64_t Len = Flat[0];
  int Hf = YONA_FLAT_HF(Flat), Off = YONA_FLAT_OFF(Flat);
  Rbt *R = rbtAllocCons();
  R->Length = Len + 1;
  R->HeapFlag = Hf;
  R->HeadOff = YONA_SEQUENCE_BRANCHING_FACTOR - 1;
  R->HeadCnt = 1;
  R->HeadBuf[YONA_SEQUENCE_BRANCHING_FACTOR - 1] = Elem;
  RbtChunk *C = chunkAlloc();
  C->Count = Len;
  memcpy(C->Elems, Flat + YONA_SEQ_HDR_SIZE + Off,
         (size_t)Len * sizeof(int64_t));
  retainHeapRange(Hf, C->Elems, Len);
  R->HeadNext = C;
  R->HeadChainLen = Len;
  return R;
}

static Rbt *flatToRbtForSnoc(int64_t *Flat, int64_t Elem) {
  int64_t Len = Flat[0];
  int Hf = YONA_FLAT_HF(Flat), Off = YONA_FLAT_OFF(Flat);
  /* Note: snoc promotions should use offset-adjusted elements */
  int64_t *Base = Flat + YONA_SEQ_HDR_SIZE + Off;
  Rbt *R = rbtAllocZeroed();
  R->Length = Len + 1;
  R->HeapFlag = Hf;
  if (Len <= YONA_SEQUENCE_BRANCHING_FACTOR) {
    R->HeadOff = 0;
    R->HeadCnt = Len;
    memcpy(R->HeadBuf, Base, (size_t)Len * sizeof(int64_t));
    retainHeapRange(Hf, R->HeadBuf, Len);
  } else {
    R->HeadOff = 0;
    R->HeadCnt = YONA_SEQUENCE_BRANCHING_FACTOR;
    memcpy(R->HeadBuf, Base, YONA_SEQUENCE_BRANCHING_FACTOR * sizeof(int64_t));
    retainHeapRange(Hf, R->HeadBuf, YONA_SEQUENCE_BRANCHING_FACTOR);
    RbtChunk *C = chunkAlloc();
    C->Count = Len - YONA_SEQUENCE_BRANCHING_FACTOR;
    memcpy(C->Elems, Base + YONA_SEQUENCE_BRANCHING_FACTOR,
           (size_t)(Len - YONA_SEQUENCE_BRANCHING_FACTOR) * sizeof(int64_t));
    retainHeapRange(Hf, C->Elems, Len - YONA_SEQUENCE_BRANCHING_FACTOR);
    R->HeadNext = C;
    R->HeadChainLen = Len - YONA_SEQUENCE_BRANCHING_FACTOR;
  }
  R->TailCnt = 1;
  R->TailBuf[0] = Elem;
  return R;
}

/* ===== Cons (prepend) — O(1) amortized ===== */

/* Callee-borrows: seq's refcount is NOT modified. The codegen is
 * responsible for managing the input's lifetime — it emits rc_dec
 * for anonymous intermediates after cons returns. This preserves the
 * unique-owner (rc==1) in-place mutation path which is critical for
 * foldl+cons patterns (e.g. reverse, build). */
int64_t *YonaRuntimeSequencePrepend(int64_t Value, int64_t *Sequence) {
  int64_t Len = Sequence[0];

  if (YONA_LIKELY(!isRbt(Sequence))) {
    if (YONA_LIKELY(Len < YONA_SEQUENCE_BRANCHING_FACTOR)) {
      int Off = YONA_FLAT_OFF(Sequence);
      int Hf = YONA_FLAT_HF(Sequence);

      /* If there's offset space, prepend into it (O(1), no copy) */
      if (Off > 0) {
        int64_t *Hdr = Sequence - 2;
        if (__atomic_load_n(&Hdr[0], __ATOMIC_ACQUIRE) == 1 &&
            Hdr[0] != YONA_RC_ARENA_SENTINEL) {
          Sequence[YONA_SEQ_HDR_SIZE + Off - 1] = Value;
          Sequence[0] = Len + 1;
          YONA_FLAT_SET_OFF_HF(Sequence, Off - 1, Hf);
          return Sequence;
        }
      }

      /* No offset space — copy with elem prepended */
      int64_t *Res = YonaRuntimeSequenceAllocate(Len + 1);
      Res[YONA_SEQ_HDR_SIZE] = Value;
      memcpy(Res + YONA_SEQ_HDR_SIZE + 1, Sequence + YONA_SEQ_HDR_SIZE + Off,
             (size_t)Len * sizeof(int64_t));
      YONA_FLAT_SET_OFF_HF(Res, 0, Hf);
      retainHeapRange(Hf, Res + YONA_SEQ_HDR_SIZE + 1, Len);
      return Res;
    }
    return (int64_t *)flatToRbtForCons(Sequence, Value);
  }

  Rbt *R = (Rbt *)Sequence;

  if (YONA_LIKELY(R->HeadOff > 0)) {
    /* Fast path: room in head_buf (31/32 cons calls) */
    if (YONA_LIKELY(isUnique(R))) {
      R->HeadOff--;
      R->HeadCnt++;
      R->Length++;
      R->HeadBuf[R->HeadOff] = Value;
      return (int64_t *)R;
    }
    Rbt *Nr = rbtClone(R);
    Nr->HeadOff--;
    Nr->HeadCnt++;
    Nr->Length++;
    Nr->HeadBuf[Nr->HeadOff] = Value;
    return (int64_t *)Nr;
  }

  /* Head_buf full: chain it into head_next (1/32 cons calls). */
  RbtChunk *C = chunkAlloc();
  C->Count = R->HeadCnt;
  memcpy(C->Elems, R->HeadBuf, (size_t)R->HeadCnt * sizeof(int64_t));
  C->Next = R->HeadNext;

  if (isUnique(R)) {
    R->HeadNext = C;
    R->HeadChainLen += R->HeadCnt;
    R->HeadOff = YONA_SEQUENCE_BRANCHING_FACTOR - 1;
    R->HeadCnt = 1;
    R->HeadBuf[YONA_SEQUENCE_BRANCHING_FACTOR - 1] = Value;
    R->Length++;
    return (int64_t *)R;
  }
  retainHeapRange(R->HeapFlag, C->Elems, C->Count);
  retainChunkValues(R->HeapFlag, R->HeadNext);
  if (R->HeadNext)
    YonaRuntimeRetain(R->HeadNext);
  Rbt *Nr = rbtCopyBody(R);
  Nr->Length = R->Length + 1;
  Nr->HeadOff = YONA_SEQUENCE_BRANCHING_FACTOR - 1;
  Nr->HeadCnt = 1;
  Nr->HeadBuf[YONA_SEQUENCE_BRANCHING_FACTOR - 1] = Value;
  Nr->HeadNext = C;
  Nr->HeadChainLen = R->HeadChainLen + R->HeadCnt;
  return (int64_t *)Nr;
}

/* ===== Tail — O(1) amortized ===== */

/* Callee-borrows variant: seq's refcount is preserved. Used for sites that
 * keep the original seq alive after the tail call (comprehensions walking
 * a borrowed source, generic tail functions that return a fresh ref). */
int64_t *YonaRuntimeSequenceTail(int64_t *Seq) {
  int64_t Len = Seq[0];
  if (YONA_UNLIKELY(Len <= 1))
    return YonaRuntimeSequenceAllocate(0);

  if (YONA_LIKELY(!isRbt(Seq))) {
    if (YONA_LIKELY(Len <= YONA_SEQUENCE_BRANCHING_FACTOR)) {
      /* Offset-based tail: bump offset instead of memmove.
       * For unique owner, modify in place. For shared, copy. */
      int64_t *Hdr = Seq - 2;
      int Off = YONA_FLAT_OFF(Seq);
      int Hf = YONA_FLAT_HF(Seq);
      if (__atomic_load_n(&Hdr[0], __ATOMIC_ACQUIRE) == 1 &&
          Hdr[0] != YONA_RC_ARENA_SENTINEL) {
        releaseHeapValue(Hf, Seq[YONA_SEQ_HDR_SIZE + Off]);
        Seq[0] = Len - 1;
        YONA_FLAT_SET_OFF_HF(Seq, Off + 1, Hf);
        return Seq;
      }
      /* Shared: copy only the valid elements (no offset) */
      int64_t *Res = YonaRuntimeSequenceAllocate(Len - 1);
      YONA_FLAT_SET_OFF_HF(Res, 0, Hf);
      memcpy(Res + YONA_SEQ_HDR_SIZE, Seq + YONA_SEQ_HDR_SIZE + Off + 1,
             (size_t)(Len - 1) * sizeof(int64_t));
      retainHeapRange(Hf, Res + YONA_SEQ_HDR_SIZE, Len - 1);
      return Res;
    }
    /* Large flat seq (larger than one branch, from generator): promote to rbt

     * * minus first element
     */
    int Hf = YONA_FLAT_HF(Seq), Off = YONA_FLAT_OFF(Seq);
    int64_t *Base = Seq + YONA_SEQ_HDR_SIZE + Off;
    Rbt *R = rbtAllocCons();
    R->Length = Len - 1;
    R->HeapFlag = Hf;
    int64_t Remain = Len - 1;
    if (Remain <= YONA_SEQUENCE_BRANCHING_FACTOR) {
      R->HeadOff = 0;
      R->HeadCnt = Remain;
      memcpy(R->HeadBuf, Base + 1, (size_t)Remain * sizeof(int64_t));
      retainHeapRange(Hf, R->HeadBuf, Remain);
    } else {
      R->HeadOff = 0;
      R->HeadCnt = YONA_SEQUENCE_BRANCHING_FACTOR;
      memcpy(R->HeadBuf, Base + 1,
             YONA_SEQUENCE_BRANCHING_FACTOR * sizeof(int64_t));
      retainHeapRange(Hf, R->HeadBuf, YONA_SEQUENCE_BRANCHING_FACTOR);
      RbtChunk *Prev = NULL;
      RbtChunk *First = NULL;
      int64_t Pos = YONA_SEQUENCE_BRANCHING_FACTOR + 1;
      int64_t ChainTotal = 0;
      while (Pos < Len) {
        RbtChunk *C = chunkAlloc();
        int64_t N = Len - Pos;
        if (N > YONA_SEQUENCE_BRANCHING_FACTOR)
          N = YONA_SEQUENCE_BRANCHING_FACTOR;
        C->Count = N;
        memcpy(C->Elems, Base + Pos, (size_t)N * sizeof(int64_t));
        retainHeapRange(Hf, C->Elems, N);
        ChainTotal += N;
        if (!First)
          First = C;
        if (Prev)
          Prev->Next = C;
        Prev = C;
        Pos += N;
      }
      R->HeadNext = First;
      R->HeadChainLen = ChainTotal;
    }
    return (int64_t *)R;
  }

  Rbt *R = (Rbt *)Seq;

  if (YONA_LIKELY(R->HeadCnt > 1)) {
    /* Fast path: bump head_off (31/32 tail calls) */
    if (YONA_LIKELY(isUnique(R))) {
      releaseHeapValue(R->HeapFlag, R->HeadBuf[R->HeadOff]);
      R->HeadOff++;
      R->HeadCnt--;
      R->Length--;
      return (int64_t *)R;
    }
    Rbt *Nr = rbtClone(R);
    releaseHeapValue(Nr->HeapFlag, Nr->HeadBuf[Nr->HeadOff]);
    Nr->HeadOff++;
    Nr->HeadCnt--;
    Nr->Length--;
    return (int64_t *)Nr;
  }

  /* head_cnt == 1: exhausted, pull from chain (1/32 tail calls) */
  if (R->HeadNext) {
    RbtChunk *C = R->HeadNext;
    if (isUnique(R)) {
      /* Unique path: r survives, with r->head_next rewritten from c
       * to c->next. Ownership transfers — r used to reach c->next
       * through c, now reaches it directly; still one owner, so
       * c->next's rc is unchanged. c itself loses its owner (r) and
       * must be freed, but c also holds a ref to c->next we don't
       * want to double-count, so sever c->next before rc_dec(c).
       *
       * Previously this was rc_inc(c) + rc_inc(c->next) + rc_dec(c),
       * which netted +1 on c->next per pop. Over a 10K-element foldl
       * that leaked ~311 chunks — the list_* benchmark RBT leak. */
      releaseHeapValue(R->HeapFlag, R->HeadBuf[R->HeadOff]);
      R->HeadOff = C->Offset;
      R->HeadCnt = C->Count;
      memcpy(R->HeadBuf, C->Elems,
             YONA_SEQUENCE_BRANCHING_FACTOR * sizeof(int64_t));
      R->HeadChainLen -= C->Count;
      R->HeadNext = C->Next;
      C->Next = NULL;
      YonaRuntimeRelease(C);
      R->Length--;
      return (int64_t *)R;
    }
    Rbt *Nr = rbtCopyBody(R);
    Nr->Length = R->Length - 1;
    Nr->HeadOff = C->Offset;
    Nr->HeadCnt = C->Count;
    memcpy(Nr->HeadBuf, C->Elems,
           YONA_SEQUENCE_BRANCHING_FACTOR * sizeof(int64_t));
    retainHeapRange(Nr->HeapFlag, Nr->HeadBuf + Nr->HeadOff, Nr->HeadCnt);
    Nr->HeadNext = C->Next;
    if (C->Next)
      YonaRuntimeRetain(C->Next);
    retainChunkValues(Nr->HeapFlag, C->Next);
    Nr->HeadChainLen = R->HeadChainLen - C->Count;
    return (int64_t *)Nr;
  }

  /* Head chain empty. Try back trie (using back_off to skip consumed). */
  int64_t Ta = trieActive(R);
  if (Ta > 0) {
    const int Unique = isUnique(R);
    Rbt *Nr = Unique ? R : rbtClone(R);
    if (Unique)
      releaseHeapValue(R->HeapFlag, R->HeadBuf[R->HeadOff]);
    else
      releaseHeapValue(Nr->HeapFlag, Nr->HeadBuf[Nr->HeadOff]);
    int64_t N = (Ta >= YONA_SEQUENCE_BRANCHING_FACTOR)
                    ? YONA_SEQUENCE_BRANCHING_FACTOR
                    : Ta;
    Nr->HeadOff = 0;
    Nr->HeadCnt = N;
    for (int64_t I = 0; I < N; I++)
      Nr->HeadBuf[I] = trieGet(R->BackRoot, R->BackShift, R->BackOff + I);
    retainHeapRange(Nr->HeapFlag, Nr->HeadBuf, N);
    Nr->BackOff += N;
    Nr->Length--;
    return (int64_t *)Nr;
  }

  /* Back trie empty. Only tail_buf remains. */
  if (R->TailCnt == 0)
    return YonaRuntimeSequenceAllocate(0);
  int64_t NewLen = R->TailCnt;
  int64_t *Res = YonaRuntimeSequenceAllocate(NewLen);
  Res[1] = R->HeapFlag;
  memcpy(Res + YONA_SEQ_HDR_SIZE, R->TailBuf, (size_t)NewLen * sizeof(int64_t));
  retainHeapRange(R->HeapFlag, Res + YONA_SEQ_HDR_SIZE, NewLen);
  return Res;
}

/* Callee-consumes variant: the caller transfers ownership of `seq`, and
 * gets back an owned tail. If seq_tail returned `seq` itself (in-place
 * offset bump on a unique seq), ownership passes through. Otherwise the
 * old seq is now dead from the caller's perspective, so we rc_dec it.
 *
 * Used by pattern-match head-tail (`case s of [h|t] -> … end`): binding
 * `t` to the tail of `s` doesn't semantically keep `s` alive in the arm.
 * With this variant, the callee drops `s` on the copy path instead of
 * the arm codegen needing to rc_inc-the-scrutinee-for-safety (which
 * forced every tail onto the copy path and regressed list_* perf).
 *
 * When the arm body DOES reference the scrutinee by name, the codegen
 * pre-rc_inc's before calling this — the rc_dec inside just balances
 * that inc, leaving the scrutinee alive for further uses. */
int64_t *YonaRuntimeSequenceConsumeTail(int64_t *Seq) {
  int64_t *Res = YonaRuntimeSequenceTail(Seq);
  if (Res != Seq)
    YonaRuntimeRelease(Seq);
  return Res;
}

/* ===== Snoc (append) — O(1) amortized ===== */

/* Callee-borrows (same as cons). */
int64_t *YonaRuntimeSequenceAppend(int64_t *Sequence, int64_t Value) {
  int64_t Len = Sequence[0];

  if (!isRbt(Sequence)) {
    if (Len < YONA_SEQUENCE_BRANCHING_FACTOR) {
      int64_t *Res = YonaRuntimeSequenceAllocate(Len + 1);
      memcpy(Res + YONA_SEQ_HDR_SIZE, Sequence + YONA_SEQ_HDR_SIZE,
             (size_t)Len * sizeof(int64_t));
      Res[YONA_SEQ_HDR_SIZE + Len] = Value;
      Res[1] = Sequence[1];
      retainHeapRange(YONA_FLAT_HF(Sequence), Res + YONA_SEQ_HDR_SIZE, Len);
      return Res;
    }
    return (int64_t *)flatToRbtForSnoc(Sequence, Value);
  }

  Rbt *R = (Rbt *)Sequence;

  if (R->TailCnt < YONA_SEQUENCE_BRANCHING_FACTOR) {
    if (isUnique(R)) {
      R->TailBuf[R->TailCnt++] = Value;
      R->Length++;
      return (int64_t *)R;
    }
    Rbt *Nr = rbtClone(R);
    Nr->TailBuf[Nr->TailCnt++] = Value;
    Nr->Length++;
    return (int64_t *)Nr;
  }

  /* Tail buf full: push into back trie */
  if (isUnique(R)) {
    void *OldRoot = R->BackRoot;
    triePushBuf(R, R->TailBuf);
    if (OldRoot && OldRoot != R->BackRoot)
      YonaRuntimeRelease(OldRoot);
    R->TailCnt = 1;
    R->TailBuf[0] = Value;
    R->Length++;
    return (int64_t *)R;
  }
  Rbt *Nr = rbtClone(R);
  triePushBuf(Nr, R->TailBuf);
  Nr->TailCnt = 1;
  Nr->TailBuf[0] = Value;
  Nr->Length++;
  return (int64_t *)Nr;
}

/* ===== Membership / difference ===== */

int64_t YonaRuntimeSequenceContains(int64_t *Seq, int64_t Elem) {
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  for (int64_t I = 0; I < Len; I++) {
    if (YonaRuntimeSequenceGet(Seq, I) == Elem)
      return 1;
  }
  return 0;
}

/* Callee-borrows (same as join). Removes every element of Right from Left. */
int64_t *YonaRuntimeSequenceDifference(int64_t *Left, int64_t *Right) {
  int64_t LeftLength = YonaRuntimeSequenceLength(Left);
  if (LeftLength == 0 || YonaRuntimeSequenceLength(Right) == 0) {
    YonaRuntimeRetain(Left);
    return Left;
  }
  int64_t Keep = 0;
  for (int64_t I = 0; I < LeftLength; I++) {
    if (!YonaRuntimeSequenceContains(Right, YonaRuntimeSequenceGet(Left, I)))
      Keep++;
  }
  if (Keep == LeftLength) {
    YonaRuntimeRetain(Left);
    return Left;
  }
  int64_t *Res = YonaRuntimeSequenceAllocate(Keep);
  int64_t Hf = isRbt(Left) ? ((Rbt *)Left)->HeapFlag : YONA_FLAT_HF(Left);
  Res[1] = Hf;
  int64_t J = 0;
  for (int64_t I = 0; I < LeftLength; I++) {
    int64_t E = YonaRuntimeSequenceGet(Left, I);
    if (!YonaRuntimeSequenceContains(Right, E))
      Res[YONA_SEQ_HDR_SIZE + J++] = E;
  }
  retainHeapRange(Hf, Res + YONA_SEQ_HDR_SIZE, Keep);
  return Res;
}

/* ===== Join (concat) — O(n) ===== */

/* Callee-borrows (same as cons). */
int64_t *YonaRuntimeSequenceJoin(int64_t *Left, int64_t *Right) {
  int64_t LeftLength = YonaRuntimeSequenceLength(Left);
  int64_t RightLength = YonaRuntimeSequenceLength(Right);
  if (LeftLength == 0) {
    YonaRuntimeRetain(Right);
    return Right;
  }
  if (RightLength == 0) {
    YonaRuntimeRetain(Left);
    return Left;
  }
  int64_t *Res = YonaRuntimeSequenceAllocate(LeftLength + RightLength);
  /* Propagate heap_flag from either operand */
  int64_t LeftHeapFlag = isRbt(Left) ? ((Rbt *)Left)->HeapFlag : Left[1];
  int64_t RightHeapFlag = isRbt(Right) ? ((Rbt *)Right)->HeapFlag : Right[1];
  Res[1] = LeftHeapFlag | RightHeapFlag;
  for (int64_t I = 0; I < LeftLength; I++)
    Res[YONA_SEQ_HDR_SIZE + I] = YonaRuntimeSequenceGet(Left, I);
  for (int64_t I = 0; I < RightLength; I++)
    Res[YONA_SEQ_HDR_SIZE + LeftLength + I] = YonaRuntimeSequenceGet(Right, I);
  if (LeftHeapFlag)
    retainHeapRange(1, Res + YONA_SEQ_HDR_SIZE, LeftLength);
  if (RightHeapFlag)
    retainHeapRange(1, Res + YONA_SEQ_HDR_SIZE + LeftLength, RightLength);
  return Res;
}

/* ===== Print ===== */

/* Defined by the runtime core; dispatches on the RC type tag. */

void YonaRuntimePrintSequence(int64_t *Seq) {
  int64_t Len = YonaRuntimeSequenceLength(Seq);
  int Hf = isRbt(Seq) ? (int)((Rbt *)Seq)->HeapFlag : YONA_FLAT_HF(Seq);
  printf("[");
  for (int64_t I = 0; I < Len; I++) {
    if (I > 0)
      printf(", ");
    int64_t Elem = YonaRuntimeSequenceGet(Seq, I);
    if (Hf)
      YonaRuntimePrintHeapValue(Elem);
    else
      printf("%" PRId64, Elem);
  }
  printf("]");
}
