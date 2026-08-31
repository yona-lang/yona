/*
 * Yona Runtime Library
 *
 * Linked with compiled Yona programs. Provides runtime support
 * for operations that can't be expressed as pure LLVM IR:
 * printing, string operations, memory management.
 */

#if defined(_WIN32)
#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif
#endif

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif
#include "Runtime/Collections/HamtInternal.h"
#include "Runtime/Core/Internal.h"
#include "yona/Runtime/Codecs/Regex.h"
#include "yona/Runtime/Collections/Arrays.h"
#include "yona/Runtime/Collections/Dictionary.h"
#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Collections/Set.h"
#include "yona/Runtime/Concurrency/Channel.h"
#include "yona/Runtime/Core/Api.h"
#if defined(_WIN32)
#include "yona/Runtime/Platform/Windows.h"
#endif

#include <setjmp.h>
#include <time.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__has_include)
#if __has_include("yona/Support/Version.h")
#include "yona/Support/Version.h"
#endif
#endif
#ifndef YONA_VERSION_STRING
#define YONA_VERSION_STRING "unknown"
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#define YONA_HAS_BACKTRACE 1
#elif defined(_WIN32)
#include <dbghelp.h>
#define YONA_HAS_BACKTRACE 1
#endif

/* ===== Reference Counting Memory Management ===== */
/*
 * RC Header Layout (hidden before the returned payload pointer):
 *   [refcount: int64_t][type_tag: int64_t][...payload...]
 *                                         ^-- returned pointer
 *
 * refcount is at ptr[-2], type_tag is at ptr[-1].
 */

#define YONA_RC_TYPE_SEQ 1
#define YONA_RC_TYPE_DICT 3
#define YONA_RC_TYPE_ADT 4
/* HAMT print/RC aux bits live in the type_tag word (see hamt.c). */
void YonaRuntimeHamtDestroyChildren(void *Node);
void YonaRuntimeHamtStampAuxiliaryFlags(void *Node, int64_t Flags);
#define YONA_RC_TYPE_CLOSURE 5
#define YONA_RC_TYPE_STRING 6
#define YONA_RC_TYPE_INT_ARRAY 18
#define YONA_RC_TYPE_FLOAT_ARRAY 19
#define YONA_RC_TYPE_NATIVE_STATE 21

/* ===== Size-class pool allocator ===== */
/* Free-list pools for common allocation sizes. Avoids malloc/free overhead
 * for frequently allocated objects (closures, seq chunks, small ADTs).
 * Each pool is a linked list of freed blocks, reused on next alloc. */

#define YONA_POOL_CLASSES 5
static const size_t PoolSizes[YONA_POOL_CLASSES] = {
    32,  /* small closures (5-slot header) */
    64,  /* small ADTs, tuples, flat seqs */
    128, /* medium closures with captures */
    296, /* RBT trie nodes, leaves, head chunks (272-296 bytes with RC header)
          */
    608, /* rbt_t root struct (592 bytes payload + 16 RC header) */
};

typedef struct PoolBlock {
  struct PoolBlock *Next;
} PoolBlock;

static _Thread_local PoolBlock *PoolFreelist[YONA_POOL_CLASSES] = {0};

static int poolClassFor(size_t TotalBytes) {
#ifdef YONA_DISABLE_POOL
  (void)TotalBytes;
  return -1;
#else
  for (int I = 0; I < YONA_POOL_CLASSES; I++) {
    if (TotalBytes <= PoolSizes[I])
      return I;
  }
  return -1; /* too large for pools */
#endif
}

/* Slab allocator: instead of individual malloc per block, allocate slabs
 * of many blocks at once. Blocks within a slab never go through glibc's
 * free/tcache, eliminating the tcache corruption issue. */
#define YONA_SLAB_BLOCKS 256 /* blocks per slab */

typedef struct Slab {
  struct Slab *Next;
  char Data[]; /* flexible array of SLAB_BLOCKS * block_size bytes */
} Slab;

static _Thread_local Slab *PoolSlabs[YONA_POOL_CLASSES] = {0};

/* Allocation statistics — enabled when YONA_ALLOC_STATS env var is set at
 * program start. The atomic increments are cheap; the report destructor
 * prints to stderr only if the env var is set. Per-tag breakdown helps
 * localize leaks (see bench/core/queens.yona investigation that uncovered
 * the seq_tail copy-path leak). */
#include <stdatomic.h>
static _Atomic long long yona_alloc_n = 0;
static _Atomic long long yona_free_n = 0;
static _Atomic long long yona_slab_bytes = 0;
#define YONA_NUM_TAGS 32
static _Atomic long long yona_alloc_by_tag[YONA_NUM_TAGS];
static _Atomic long long yona_free_by_tag[YONA_NUM_TAGS];
static const char *yonaTagName(int Tag) {
  switch (Tag) {
  case 1:
    return "SEQ";
  case 3:
    return "DICT";
  case 4:
    return "ADT";
  case 5:
    return "CLOSURE";
  case 6:
    return "STRING";
  case 7:
    return "TUPLE";
  case 8:
    return "BYTEARR";
  case 12:
    return "RBT";
  case 13:
    return "RBT_NODE";
  case 14:
    return "RBT_LEAF";
  case 15:
    return "RBT_CHUNK";
  case 16:
    return "REGEX";
  case 17:
    return "PROCESS";
  case 18:
    return "INTARR";
  case 19:
    return "FLOATARR";
  case 20:
    return "CHANNEL";
  default:
    return "other";
  }
}
/* Register via atexit so we run before the C runtime tears down stdio.
 * On Windows, __attribute__((destructor)) executes after CRT stream
 * shutdown and crashed when this function tried to fprintf(stderr, …). */
static void yonaAllocReport(void) {
  if (!getenv("YONA_ALLOC_STATS"))
    return;
  fprintf(stderr, "[alloc-stats] allocs=%lld frees=%lld slab_bytes=%lld\n",
          atomic_load(&yona_alloc_n), atomic_load(&yona_free_n),
          atomic_load(&yona_slab_bytes));
  for (int T = 0; T < YONA_NUM_TAGS; T++) {
    long long A = atomic_load(&yona_alloc_by_tag[T]);
    long long F = atomic_load(&yona_free_by_tag[T]);
    if (A == 0 && F == 0)
      continue;
    fprintf(stderr,
            "[alloc-stats]   tag=%s allocs=%lld frees=%lld leaked=%lld\n",
            yonaTagName(T), A, F, A - F);
  }
  fflush(stderr);
}
static _Atomic int YonaAllocReportRegistered = 0;
static inline void yonaAllocReportMaybeRegister(void) {
  int Expected = 0;
  if (atomic_compare_exchange_strong_explicit(
          &YonaAllocReportRegistered, &Expected, 1, memory_order_acq_rel,
          memory_order_acquire)) {
    atexit(yonaAllocReport);
  }
}
#define YONA_ALLOC_INC_TAG(tag)                                                \
  do {                                                                         \
    atomic_fetch_add_explicit(&yona_alloc_n, 1, memory_order_relaxed);         \
    if ((tag) >= 0 && (tag) < YONA_NUM_TAGS)                                   \
      atomic_fetch_add_explicit(&yona_alloc_by_tag[(tag)], 1,                  \
                                memory_order_relaxed);                         \
  } while (0)
#define YONA_FREE_INC_TAG(tag)                                                 \
  do {                                                                         \
    atomic_fetch_add_explicit(&yona_free_n, 1, memory_order_relaxed);          \
    if ((tag) >= 0 && (tag) < YONA_NUM_TAGS)                                   \
      atomic_fetch_add_explicit(&yona_free_by_tag[(tag)], 1,                   \
                                memory_order_relaxed);                         \
  } while (0)
#define YONA_SLAB_ADD(n)                                                       \
  atomic_fetch_add_explicit(&yona_slab_bytes, (long long)(n),                  \
                            memory_order_relaxed)

static void poolGrow(int Cls) {
  size_t BlockSize = PoolSizes[Cls];
  size_t SlabSize = sizeof(Slab) + YONA_SLAB_BLOCKS * BlockSize;
  Slab *NewSlab = (Slab *)calloc(1, SlabSize);
  NewSlab->Next = PoolSlabs[Cls];
  PoolSlabs[Cls] = NewSlab;
  YONA_SLAB_ADD(SlabSize);
  if (getenv("YONA_POOL_TRACE"))
    fprintf(stderr, "pool_grow cls=%d slab=%p data=%p-%p\n", Cls,
            (void *)NewSlab, (void *)NewSlab->Data,
            (void *)(NewSlab->Data + YONA_SLAB_BLOCKS * BlockSize));
  /* Link all blocks in the slab into the free list */
  for (int I = 0; I < YONA_SLAB_BLOCKS; I++) {
    PoolBlock *Block = (PoolBlock *)(NewSlab->Data + I * BlockSize);
    Block->Next = PoolFreelist[Cls];
    PoolFreelist[Cls] = Block;
  }
}

static void *poolAlloc(size_t TotalBytes) {
  int Cls = poolClassFor(TotalBytes);
  if (Cls < 0)
    return malloc(TotalBytes);
  if (!PoolFreelist[Cls])
    poolGrow(Cls);
  PoolBlock *Block = PoolFreelist[Cls];
  if (((uintptr_t)Block & 7) != 0) {
    fprintf(stderr, "POOL_ALLOC: UNALIGNED head block=%p cls=%d\n",
            (void *)Block, Cls);
    fflush(stderr);
    abort();
  }
  // Check that next pointer (about to become new head) is also aligned
  PoolBlock *Next = Block->Next;
  if (Next && ((uintptr_t)Next & 7) != 0) {
    fprintf(stderr,
            "POOL_ALLOC: popping block=%p with UNALIGNED next=%p cls=%d\n",
            (void *)Block, (void *)Next, Cls);
    fflush(stderr);
    abort();
  }
  PoolFreelist[Cls] = Next;
  return (void *)Block;
}

static void poolFree(void *Ptr, size_t TotalBytes) {
  if (!Ptr)
    return;
  int Cls = poolClassFor(TotalBytes);
  if (Cls < 0) {
    free(Ptr);
    return;
  }
  if (Cls == 3 && getenv("YONA_POOL_TRACE"))
    fprintf(stderr, "pool_free cls=3 block=%p\n", Ptr);
  PoolBlock *Block = (PoolBlock *)Ptr;
  Block->Next = PoolFreelist[Cls];
  PoolFreelist[Cls] = Block;
}

/* Internal: allocate with RC header, returns pointer to payload.
 * Header: [refcount, type_tag_and_pool_class, ...payload...]
 *                                              ^-- returned pointer
 * Pool class is encoded in the upper bits of the type_tag word:
 *   bits 0-7:  type tag (RC_TYPE_SEQ, etc.)
 *   bits 8-15: pool class index + 1 (0 = not pooled, 1-4 = class 0-3)
 * This avoids a 3rd header word while supporting pool_free. */
#define YONA_RC_HEADER_SIZE 2

#define YONA_ENCODE_TAG(tag, cls) ((tag) | (((int64_t)(cls) + 1) << 8))
#define YONA_ENCODE_TAG_LEN(tag, cls, len)                                     \
  ((tag) | (((int64_t)(cls) + 1) << 8) | ((int64_t)(len) << 16))
#define YONA_DECODE_TAG(encoded) ((encoded) & 0xFF)
#define YONA_DECODE_POOL_CLASS(encoded) ((int)(((encoded) >> 8) & 0xFF) - 1)
#define YONA_DECODE_STRING_LEN(encoded) ((size_t)((encoded) >> 16))

void *YonaRuntimeAllocate(int64_t TypeTag, size_t PayloadBytes) {
  yonaAllocReportMaybeRegister();
  size_t Total = YONA_RC_HEADER_SIZE * sizeof(int64_t) + PayloadBytes;
  int Cls = poolClassFor(Total);
  int64_t *Raw = (int64_t *)poolAlloc(Total);
  Raw[0] = 1;                             /* refcount = 1 */
  Raw[1] = YONA_ENCODE_TAG(TypeTag, Cls); /* type tag + pool class */
  YONA_ALLOC_INC_TAG((int)TypeTag);
  return (void *)(Raw + YONA_RC_HEADER_SIZE);
}

/* Public: increment refcount (atomic for thread safety) */
void YonaRuntimeRetain(void *Value) {
  if (__builtin_expect(!Value, 0))
    return;
  int64_t *Header = ((int64_t *)Value) - YONA_RC_HEADER_SIZE;
  int64_t Rc = __atomic_load_n(&Header[0], __ATOMIC_RELAXED);
  if (__builtin_expect(Rc == INT64_MAX, 0))
    return; /* arena/static sentinel */
  __atomic_fetch_add(&Header[0], 1, __ATOMIC_RELAXED);
}

/* Sentinel refcount for arena-allocated objects. rc_dec skips these. */
#define YONA_RC_ARENA_SENTINEL INT64_MAX

/* Forward declarations for recursive RC types */
#define YONA_RC_TYPE_RBT_FWD 12 /* RBT seq root struct */
#define YONA_RC_TYPE_RBT_NODE_FWD                                              \
  13 /* RBT internal node (32 child pointers)                                  \
      */
#define YONA_RC_TYPE_RBT_LEAF_FWD                                              \
  14 /* RBT leaf node (heap_flag + 32 elements) */

/* Public: decrement refcount; free when it reaches 0.
 * Recursively rc_dec
 * pointer-typed children for known container types. */
void YonaRuntimeRelease(void *Value) {
  if (__builtin_expect(!Value, 0))
    return;
  int64_t *Header = ((int64_t *)Value) - YONA_RC_HEADER_SIZE;
  int64_t Rc = __atomic_load_n(&Header[0], __ATOMIC_RELAXED);
  if (__builtin_expect(Rc == YONA_RC_ARENA_SENTINEL, 0))
    return;
  int64_t Old = __atomic_fetch_sub(&Header[0], 1, __ATOMIC_ACQ_REL);
  if (__builtin_expect(Old <= 1, 0)) {
    int64_t EncodedTag = Header[1];
    int64_t TypeTag = YONA_DECODE_TAG(EncodedTag);
    int PoolCls = YONA_DECODE_POOL_CLASS(EncodedTag);
    int64_t *Payload = (int64_t *)Value;

    if (TypeTag == YONA_RC_TYPE_SEQ) {
      /* Flat seq: rc_dec elements if heap_flag set.
       * Layout: [count, flags, elem0, ...]
       * flags: bits 0-31 = heap_flag, bits 32-63 = offset */
      int64_t Count = Payload[0];
      int64_t Flags = Payload[1];
      int HeapFlag = (int)(Flags & 0xFFFFFFFF);
      int Offset = (int)((uint64_t)Flags >> 32);
      if (HeapFlag) {
        for (int64_t I = 0; I < Count; I++) {
          int64_t Val = Payload[2 + Offset + I];
          if (Val)
            YonaRuntimeRelease((void *)(intptr_t)Val);
        }
      }
    } else if (TypeTag == YONA_RC_TYPE_RBT_FWD) {
      /* RBT seq root: rc_dec elements in head/tail buffers, head chain, trie.
       * Layout matches rbt_t in seq.c:
       *   [0] length, [1] heap_flag, [2] head_off, [3] head_cnt,
       *   [4..35] head_buf[32],
       *   [36] head_next (ptr), [37] head_chain_len,
       *   [38] tail_cnt, [39..70] tail_buf[32],
       *   [71] back_shift, [72] back_root (ptr),
       *   [73] back_size, [74] back_off */
      int64_t Hf = Payload[1];
      int64_t HeadOff = Payload[2];
      int64_t HeadCnt = Payload[3];
      int64_t TailCnt = Payload[38];
      if (Hf) {
        for (int64_t I = 0; I < HeadCnt; I++) {
          int64_t V = Payload[4 + HeadOff + I];
          if (V)
            YonaRuntimeRelease((void *)(intptr_t)V);
        }
        for (int64_t I = 0; I < TailCnt; I++) {
          int64_t V = Payload[39 + I];
          if (V)
            YonaRuntimeRelease((void *)(intptr_t)V);
        }
      }
      /* Walk head chain and rc_dec elements if heap-typed */
      void *HeadNext = *(void **)&Payload[36];
      if (Hf && HeadNext) {
        void *Cur = HeadNext;
        while (Cur) {
          int64_t *Cp = (int64_t *)Cur;
          int64_t Coff = Cp[0], Ccnt = Cp[1];
          for (int64_t I = 0; I < Ccnt; I++) {
            int64_t V = Cp[2 + Coff + I];
            if (V)
              YonaRuntimeRelease((void *)(intptr_t)V);
          }
          Cur = *(void **)&Cp[2 + 32];
        }
      }
      if (HeadNext)
        YonaRuntimeRelease(HeadNext);
      void *BackRoot = *(void **)&Payload[72];
      if (BackRoot)
        YonaRuntimeRelease(BackRoot);
    } else if (TypeTag == YONA_RC_TYPE_RBT_NODE_FWD) {
      /* RBT internal node: rc_dec all non-null children. */
      for (int I = 0; I < 32; I++) {
        int64_t Child = Payload[I];
        if (Child)
          YonaRuntimeRelease((void *)(intptr_t)Child);
      }
    } else if (TypeTag == 15 /* RC_TYPE_RBT_CHUNK */) {
      /* RBT head chain chunk: [offset, count, elems[32], next_ptr]
       * rc_dec next chunk. Elements are rc_dec'd by the rbt_t destructor
       * when walking the chain (it knows the heap_flag). */
      /* Note: for simplicity, don't rc_dec elements here. The rbt_t
       * destructor walks the head chain and handles element cleanup.
       * But if a chunk is freed independently (e.g. after tail detaches
       * it), we must handle elements. We use a conservative approach:
       * always rc_dec the next pointer. */
      void *Next =
          *(void **)&Payload[2 + 32]; /* after offset, count, elems[32] */
      if (Next)
        YonaRuntimeRelease(Next);
    } else if (TypeTag == YONA_RC_TYPE_RBT_LEAF_FWD) {
      /* RBT leaf node: rc_dec elements if heap_flag set.
       * Layout: [heap_flag, elem0, ..., elem31] */
      int64_t Hf = Payload[0];
      if (Hf) {
        for (int I = 0; I < 32; I++) {
          int64_t V = Payload[1 + I];
          if (V)
            YonaRuntimeRelease((void *)(intptr_t)V);
        }
      }
    } else if (TypeTag == YONA_RC_TYPE_CLOSURE) {
      /* Closure: rc_dec heap-typed captures using the heap_mask.
       * Layout: [fn_ptr, ret_type, arity, num_captures, heap_mask,
       *          borrow_mask, cap0, ...] */
      int64_t NumCaps = Payload[3];
      int64_t HeapMask = Payload[4];
      for (int64_t Ci = 0; Ci < NumCaps && Ci < 64; Ci++) {
        if (HeapMask & ((int64_t)1 << Ci)) {
          int64_t CapVal = Payload[6 + Ci];
          if (CapVal)
            YonaRuntimeRelease((void *)(intptr_t)CapVal);
        }
      }
    } else if (TypeTag == YONA_RC_TYPE_ADT) {
      /* ADT: rc_dec heap-typed fields using heap_mask.
       * Layout: [tag, num_fields, heap_mask, field0, ...] */
      int64_t NumFields = Payload[1];
      int64_t HeapMask = Payload[2];
      for (int64_t Fi = 0; Fi < NumFields && Fi < 64; Fi++) {
        if (HeapMask & ((int64_t)1 << Fi)) {
          int64_t FieldVal = Payload[3 + Fi];
          if (FieldVal)
            YonaRuntimeRelease((void *)(intptr_t)FieldVal);
        }
      }
    } else if (TypeTag == YONA_RC_TYPE_DICT) {
      /* HAMT: rc_dec heap keys/values (aux flags) and child sub-nodes. */
      YonaRuntimeHamtDestroyChildren(Value);
    } else if (TypeTag == 9 /* RC_TYPE_TUPLE */) {
      /* Tuple: rc_dec heap-typed elements using heap_mask.
       * Layout: [num_elements, heap_mask, elem0, ...] */
      int64_t NumElems = Payload[0];
      int64_t HeapMask = Payload[1];
      for (int64_t Ei = 0; Ei < NumElems && Ei < 64; Ei++) {
        if (HeapMask & ((int64_t)1 << Ei)) {
          int64_t ElemVal = Payload[2 + Ei];
          if (ElemVal)
            YonaRuntimeRelease((void *)(intptr_t)ElemVal);
        }
      }
    } else if (TypeTag == 17 /* RC_TYPE_PROCESS */) {
      /* Process handle: close pipe fds, reap zombie.
       * YonaRuntimeProcessDestroy is defined in os_linux.c. */
      extern void YonaRuntimeProcessDestroy(void *Proc) __attribute__((weak));
      if (YonaRuntimeProcessDestroy)
        YonaRuntimeProcessDestroy(Value);
    } else if (TypeTag == 16 /* RC_TYPE_REGEX */) {
      /* Regex handle: free the PCRE2 compiled pattern.
       * Layout: [pcre2_code* code]
       * YonaRuntimeRegexDisposeCompiledCode is weak — if Regex.c isn't
       *
       * linked (PCRE2 unavailable), this is a no-op. */
      void *Code = *(void **)Payload;
      if (Code) {
        extern void YonaRuntimeRegexDisposeCompiledCode(void *Code)
            __attribute__((weak));
        if (YonaRuntimeRegexDisposeCompiledCode)
          YonaRuntimeRegexDisposeCompiledCode(Code);
      }
    } else if (TypeTag == 20 /* RC_TYPE_CHANNEL */) {
      /* Channel: signal waiters, destroy mutex/condvars, free buffer. */
      YonaRuntimeChannelDestroy((YonaChannelRef)Value);
    } else if (TypeTag == YONA_RC_TYPE_NATIVE_STATE) {
      /* Opaque mutable state captured by a native iterator closure.
       * The first payload word is an optional type-specific finalizer. */
      void (*Finalize)(void *) = *(void (**)(void *))Payload;
      if (Finalize)
        Finalize(Value);
    }
    YONA_FREE_INC_TAG((int)TypeTag);
    if (PoolCls >= 0)
      poolFree(Header, PoolSizes[PoolCls]);
    else
      free(Header);
  }
}

void *YonaRuntimeNativeStateAllocate(size_t Bytes, void (*Finalize)(void *)) {
  if (Bytes < sizeof(Finalize))
    Bytes = sizeof(Finalize);
  void *State = YonaRuntimeAllocate(YONA_RC_TYPE_NATIVE_STATE, Bytes);
  *(void (**)(void *))State = Finalize;
  return State;
}

/* ===== Arena Allocator ===== */
/*
 * Bump-allocated memory block freed in bulk. Non-escaping values use
 * this instead of malloc+RC for zero-overhead deallocation.
 *
 * Layout: [yona_arena_t header][...bump-allocated payloads...]
 * Each payload has the standard RC header but with refcount=SENTINEL.
 */

#define YONA_ARENA_DEFAULT_SIZE 4096

typedef struct YonaArena {
  char *Base;
  char *Cursor;
  char *End;
  struct YonaArena *Next; /* overflow chain */
  void **Objects;
  size_t ObjectCount;
  size_t ObjectCapacity;
} YonaArena;

void *YonaRuntimeArenaCreate(int64_t Size) {
  yonaAllocReportMaybeRegister();
  if (Size <= 0)
    Size = YONA_ARENA_DEFAULT_SIZE;
  YonaArena *Arena = (YonaArena *)malloc(sizeof(YonaArena) + Size);
  Arena->Base = (char *)(Arena + 1);
  Arena->Cursor = Arena->Base;
  Arena->End = Arena->Base + Size;
  Arena->Next = NULL;
  Arena->Objects = NULL;
  Arena->ObjectCount = 0;
  Arena->ObjectCapacity = 0;
  return Arena;
}

void *YonaRuntimeArenaAllocate(void *ArenaPtr, int64_t TypeTag,
                               int64_t PayloadBytes) {
  YonaArena *Arena = (YonaArena *)ArenaPtr;
  size_t Total = YONA_RC_HEADER_SIZE * sizeof(int64_t) + (size_t)PayloadBytes;
  /* Align to 8 bytes */
  Total = (Total + 7) & ~7;

  /* Find an arena block with enough space */
  while (Arena->Cursor + Total > Arena->End) {
    if (!Arena->Next) {
      /* Allocate overflow block (at least total or default size) */
      int64_t NewSize = Total > YONA_ARENA_DEFAULT_SIZE
                            ? (int64_t)Total * 2
                            : YONA_ARENA_DEFAULT_SIZE;
      Arena->Next = (YonaArena *)YonaRuntimeArenaCreate(NewSize);
    }
    Arena = Arena->Next;
  }

  int64_t *Raw = (int64_t *)Arena->Cursor;
  Arena->Cursor += Total;
  Raw[0] = YONA_RC_ARENA_SENTINEL; /* sentinel: rc_dec will skip */
  Raw[1] = TypeTag;
  void *Object = (void *)(Raw + YONA_RC_HEADER_SIZE);
  if (Arena->ObjectCount == Arena->ObjectCapacity) {
    size_t NewCapacity = Arena->ObjectCapacity ? Arena->ObjectCapacity * 2 : 16;
    void **Objects =
        (void **)realloc(Arena->Objects, NewCapacity * sizeof(void *));
    if (!Objects) {
      fprintf(stderr, "Fatal: unable to grow arena object registry\n");
      abort();
    }
    Arena->Objects = Objects;
    Arena->ObjectCapacity = NewCapacity;
  }
  Arena->Objects[Arena->ObjectCount++] = Object;
  return Object; /* return pointer past header */
}

static void yonaArenaReleaseObjectChildren(void *Object) {
  int64_t *Header = ((int64_t *)Object) - YONA_RC_HEADER_SIZE;
  int64_t *Payload = (int64_t *)Object;
  const int64_t TypeTag = Header[1];

  /* Flat sequences are currently the only arena-allocated managed object.
   * Bulk-freeing their storage must still run the recursive ownership edge
   * represented by heap_flag. Arena children themselves have sentinel RC and
   * are visited independently through this registry. */
  if (TypeTag == YONA_RC_TYPE_SEQ) {
    const int64_t Count = Payload[0];
    const int64_t Flags = Payload[1];
    const int HeapFlag = (int)(Flags & 0xFFFFFFFF);
    const int Offset = (int)((uint64_t)Flags >> 32);
    if (HeapFlag) {
      for (int64_t I = 0; I < Count; ++I) {
        const int64_t Value = Payload[2 + Offset + I];
        if (Value)
          YonaRuntimeRelease((void *)(intptr_t)Value);
      }
    }
  }
}

void YonaRuntimeArenaDestroy(void *ArenaPtr) {
  YonaArena *Arena = (YonaArena *)ArenaPtr;
  while (Arena) {
    YonaArena *Next = Arena->Next;
    for (size_t I = 0; I < Arena->ObjectCount; ++I)
      yonaArenaReleaseObjectChildren(Arena->Objects[I]);
    free(Arena->Objects);
    free(Arena);
    Arena = Next;
  }
}

#define YONA_RC_TYPE_BOX 7
#define YONA_RC_TYPE_BYTE_ARRAY 8
/* Box: heap-allocate arbitrary data (for tuples in collections) */
void *YonaRuntimeBox(const void *Data, int64_t Size) {
  void *Box = YonaRuntimeAllocate(YONA_RC_TYPE_BOX, (size_t)Size);
  memcpy(Box, Data, (size_t)Size);
  return Box;
}

/* Tuple: heap-allocate i64 array with element metadata.
 * Layout: [num_elements, heap_mask, elem0, elem1, ...]
 * Uses RC_TYPE_TUPLE (9) for recursive destruction. */
#define YONA_RC_TYPE_TUPLE 9

void *YonaRuntimeTupleAllocate(int64_t ElementCount) {
  int64_t *Tuple = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_TUPLE, (2 + ElementCount) * sizeof(int64_t));
  Tuple[0] = ElementCount;
  Tuple[1] = 0; /* heap_mask */
  return Tuple;
}

void YonaRuntimeTupleSetHeapMask(void *Tuple, int64_t Mask) {
  ((int64_t *)Tuple)[1] = Mask;
}

void YonaRuntimeTupleSet(void *Tuple, int64_t Index, int64_t Value) {
  ((int64_t *)Tuple)[2 + Index] = Value;
}

int64_t YonaRuntimeTupleGet(void *Tuple, int64_t Index) {
  return ((int64_t *)Tuple)[2 + Index];
}

/* Unbox: just returns the pointer (data is at the payload position) */
/* No runtime function needed — codegen does inttoptr + load directly */

/* Public: allocate an RC-managed string buffer (for platform layer) */
void *YonaRuntimeAllocateString(size_t Bytes) {
  return YonaRuntimeAllocate(YONA_RC_TYPE_STRING, Bytes);
}

/* Public: allocate an RC-managed string with known length.
 * Length is encoded in bits 16-63 of the type_tag word for O(1) retrieval. */
void *YonaRuntimeAllocateStringWithLength(size_t Bytes, size_t StringLength) {
  yonaAllocReportMaybeRegister();
  size_t Total = YONA_RC_HEADER_SIZE * sizeof(int64_t) + Bytes;
  int Cls = poolClassFor(Total);
  int64_t *Raw = (int64_t *)poolAlloc(Total);
  Raw[0] = 1;
  Raw[1] = YONA_ENCODE_TAG_LEN(YONA_RC_TYPE_STRING, Cls, StringLength);
  YONA_ALLOC_INC_TAG(YONA_RC_TYPE_STRING);
  return (void *)(Raw + YONA_RC_HEADER_SIZE);
}

/* O(1) string length: reads stored length from RC header, falls back to strlen.
 */
int64_t YonaRuntimeStringLength(const char *Str) {
  if (__builtin_expect(!Str, 0))
    return 0;
  int64_t *Header = ((int64_t *)Str) - YONA_RC_HEADER_SIZE;
  /* Check if this is an RC-managed string (refcount > 0 and reasonable) */
  int64_t Rc = Header[0];
  if (__builtin_expect(Rc > 0 && Rc < 1000000, 1)) {
    size_t Len = YONA_DECODE_STRING_LEN(Header[1]);
    if (__builtin_expect(Len > 0, 1))
      return (int64_t)Len;
  }
  return (int64_t)strlen(Str);
}

void YonaRuntimePrintInt(int64_t Value) { printf("%" PRId64, Value); }

void YonaRuntimePrintFloat(double Value) { printf("%g", Value); }

void YonaRuntimePrintString(const char *Value) { printf("%s", Value); }

void YonaRuntimePrintBool(int Value) { printf("%s", Value ? "true" : "false"); }

void YonaRuntimePrintNewline(void) { printf("\n"); }

char *YonaRuntimeStringConcatenate(const char *A, const char *B) {
  size_t LenA = (size_t)YonaRuntimeStringLength(A);
  size_t LenB = (size_t)YonaRuntimeStringLength(B);
  size_t Total = LenA + LenB;
  char *Result = (char *)YonaRuntimeAllocateStringWithLength(Total + 1, Total);
  memcpy(Result, A, LenA);
  memcpy(Result + LenA, B, LenB + 1);
  return Result;
}

/* ===== Symbol runtime ===== */
/* Symbols are interned to i64 IDs at compile time. Comparison is icmp eq. */
/* Print takes the string name (resolved by the compiler from the symbol table).
 */

void YonaRuntimePrintSymbol(const char *Name) { printf(":%s", Name); }

/* Print a heap pointer stored as i64 in a seq/tuple slot. Consults the RC
 * type tag so nested Seq/String/Tuple/… print as values, not addresses. */
void YonaRuntimePrintHeapValue(int64_t Val) {
  if (!Val) {
    printf("0");
    return;
  }
  int64_t *Ptr = (int64_t *)(intptr_t)Val;
  int64_t Tag = YONA_DECODE_TAG(Ptr[-1]);
  switch ((int)Tag) {
  case YONA_RC_TYPE_SEQ:
  case YONA_RC_TYPE_RBT_FWD:
    YonaRuntimePrintSequence(Ptr);
    break;
  case YONA_RC_TYPE_STRING:
    YonaRuntimePrintString((const char *)Ptr);
    break;
  case YONA_RC_TYPE_DICT:
    if (yonaRuntimeHamtFlags((YonaHamtNode *)Ptr) & YONA_HAMT_FLAG_IS_SET)
      YonaRuntimePrintSet(Ptr);
    else
      YonaRuntimePrintDictionary(Ptr);
    break;
  case YONA_RC_TYPE_TUPLE: {
    int64_t N = Ptr[0];
    int64_t Mask = Ptr[1];
    printf("(");
    for (int64_t I = 0; I < N; I++) {
      if (I > 0)
        printf(", ");
      int64_t Elem = Ptr[2 + I];
      if (I < 64 && (Mask & ((int64_t)1 << I)))
        YonaRuntimePrintHeapValue(Elem);
      else
        printf("%" PRId64, Elem);
    }
    printf(")");
    break;
  }
  case YONA_RC_TYPE_BYTE_ARRAY:
    YonaRuntimePrintByteArray(Ptr);
    break;
  case YONA_RC_TYPE_INT_ARRAY:
    YonaRuntimePrintIntArray(Ptr);
    break;
  case YONA_RC_TYPE_FLOAT_ARRAY:
    YonaRuntimePrintFloatArray((double *)Ptr);
    break;
  case YONA_RC_TYPE_ADT:
    printf("<adt>");
    break;
  case YONA_RC_TYPE_CLOSURE:
    printf("<function>");
    break;
  default:
    printf("%" PRId64, Val);
    break;
  }
}

/* ===== ADT runtime (recursive types) ===== */
/* Heap-allocated ADT nodes: [tag (i8), field0 (i64), field1 (i64), ...] */
/* Used for recursive types like List a = Cons a (List a) | Nil        */

/* ADT layout (recursive, heap-allocated):
 * [tag, num_fields, heap_mask, field0, field1, ...]
 * tag: constructor tag (from adt_constructors_)
 * num_fields: number of fields
 * heap_mask: bitmask of which fields are heap-typed (for recursive rc_dec)
 * Fields start at index 3.
 */
#define YONA_ADT_HDR_SIZE 3 /* tag, num_fields, heap_mask */

/* Helper: allocate a None Option ADT */
int64_t *YonaRuntimeMakeNone(void) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_ADT, YONA_ADT_HDR_SIZE * sizeof(int64_t));
  Adt[0] = 1;
  Adt[1] = 0;
  Adt[2] = 0;
  return Adt;
}

/* Helper: allocate a Some(value) Option ADT.
 * heap_mask is 0 — the element is NOT owned by the wrapper.
 * Callers must rc_dec the element separately. This allows extracting
 * the element and freeing the wrapper without a use-after-free. */
int64_t *YonaRuntimeMakeSome(int64_t Value) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_ADT, (YONA_ADT_HDR_SIZE + 1) * sizeof(int64_t));
  Adt[0] = 0;
  Adt[1] = 1;
  Adt[2] = 0;
  Adt[3] = Value;
  return Adt;
}

/* Helper: allocate an Iterator ADT wrapping a closure */
int64_t *YonaRuntimeMakeIterator(int64_t *Closure) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_ADT, (YONA_ADT_HDR_SIZE + 1) * sizeof(int64_t));
  Adt[0] = 0;
  Adt[1] = 1;
  Adt[2] = 1;
  Adt[3] = (int64_t)(intptr_t)Closure;
  return Adt;
}

void *YonaRuntimeAdtAllocate(int64_t Tag, int64_t FieldCount) {
  int64_t *Value = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_ADT, (YONA_ADT_HDR_SIZE + FieldCount) * sizeof(int64_t));
  Value[0] = Tag;
  Value[1] = FieldCount;
  Value[2] = 0; /* heap_mask — set by codegen */
  return Value;
}

void YonaRuntimeAdtSetHeapMask(void *Value, int64_t Mask) {
  ((int64_t *)Value)[2] = Mask;
}

int64_t YonaRuntimeAdtGetTag(void *Value) { return ((int64_t *)Value)[0]; }

int64_t YonaRuntimeAdtGetField(void *Value, int64_t Index) {
  return ((int64_t *)Value)[YONA_ADT_HDR_SIZE + Index];
}

void YonaRuntimeAdtSetField(void *Value, int64_t Index, int64_t Field) {
  ((int64_t *)Value)[YONA_ADT_HDR_SIZE + Index] = Field;
}
