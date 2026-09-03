#include "yona/Runtime/Concurrency/Async.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/SjLj.h"

#if defined(_WIN32)
#include "yona/Runtime/Platform/Windows.h"

#include <dbghelp.h>
#else
#include <execinfo.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Exception handling (setjmp/longjmp) ===== */

#define YONA_MAX_TRY_DEPTH 256

typedef struct {
  int64_t Symbol;
  const char *Message;
  void *Owner;
} YonaException;

/* ===== Perceus phase 3: frame-scoped heap cleanup on unwind =====
 *
 * Each user-defined function with heap params allocates a
 * `yona_frame_t` on its own stack (via codegen alloca) and chains it
 * into the thread-local `yona_current_frame`. On normal exit the frame
 * is popped and cleanup happens via the codegen's function-exit rc_dec
 * loop. On raise, YonaRuntimeRaise walks frames from the current one back
 * to the frame that was active when the surrounding try block ran
 * setjmp, rc_dec'ing any drops that haven't been transferred.
 *
 * Codegen emits:
 *   - alloca yona_frame_t
 *   - fills f->drops[0..N-1] with heap-typed param pointers, sets
 *     f->drop_count = N
 *   - calls YonaRuntimeFramePush(f) after entry-time rc setup
 *   - before returning normally, calls YonaRuntimeFramePop(f)
 *   - at each transfer site (single-use SEQ/SET/DICT arg handed to
 *     a callee), calls YonaRuntimeFrameTransfer(ptr) to NULL that slot
 *     so unwind won't double-dec something the callee now owns.
 *
 * The type tag we feed to the destructor lives in the RC header at
 * `ptr - 16`; `YonaRuntimeRelease` reads it, so the frame only needs to
 * hold raw pointers.
 */

#define YONA_MAX_FRAME_DROPS 16

typedef struct YonaFrame {
  struct YonaFrame *Prev;
  int DropCount;
  int Pad;
  void *Drops[YONA_MAX_FRAME_DROPS];
} YonaFrame;

_Thread_local YonaFrame *YonaCurrentFrame = NULL;

/* The shared target-aware SJLJ context. Non-AArch64 targets use LLVM/GCC's
 * five-word buffer; AArch64 additionally preserves the ABI-callee-saved
 * registers that a long jump bypasses restoring through normal epilogues. */

typedef struct {
  YonaSjLjBufT Buf[YONA_MAX_TRY_DEPTH];
  int Depth;
  YonaException Current;
  YonaFrame *SavedFrame[YONA_MAX_TRY_DEPTH];
} YonaExceptionState;

static _Thread_local YonaExceptionState YonaExc = {.Depth = 0};

/* Exposed as a TLS global for zero-overhead depth checks from codegen.
 * The codegen declares this as a thread_local i32 and loads it inline
 * (no function call) to branch around frame setup. */
_Thread_local int YonaTryDepth = 0;

int YonaRuntimeTryDepth(void) { return YonaTryDepth; }

/* Task-group arena bindings: wholesale-free bump memory on raise unwind.
 * Each push records the task group and yona_try_depth at entry; raise
 * destroys arenas for bindings deeper than the catch we're jumping to. */
#define YONA_MAX_GROUP_ARENA_BIND 32
typedef struct {
  void *Group; /* YonaTaskGroup* */
  int TryDepth;
} YonaGroupArenaBinding;

static _Thread_local YonaGroupArenaBinding
    YonaGroupArenaStack[YONA_MAX_GROUP_ARENA_BIND];
static _Thread_local int YonaGroupArenaSp = 0;

void YonaRuntimeTaskGroupArenaBindPush(YonaTaskGroupRef Group) {
  if (YonaGroupArenaSp >= YONA_MAX_GROUP_ARENA_BIND) {
    fprintf(stderr, "Fatal: task-group arena bind stack overflow\n");
    abort();
  }
  YonaGroupArenaStack[YonaGroupArenaSp].Group = Group;
  YonaGroupArenaStack[YonaGroupArenaSp].TryDepth = YonaTryDepth;
  YonaGroupArenaSp++;
}

void YonaRuntimeTaskGroupArenaBindPop(void) {
  if (YonaGroupArenaSp <= 0)
    return;
  YonaGroupArenaSp--;
}

static void unwindTaskGroupArenasTo(int TargetTryDepth) {
  while (YonaGroupArenaSp > 0) {
    YonaGroupArenaBinding *Top = &YonaGroupArenaStack[YonaGroupArenaSp - 1];
    if (Top->TryDepth <= TargetTryDepth)
      break;
    void *G = Top->Group;
    YonaGroupArenaSp--;
    /* Full group teardown (arena + mutex + free), matching codegen's
     * normal group_end path — must run before longjmp skips it. */
    YonaRuntimeTaskGroupEnd(G);
  }
}

void YonaRuntimeFramePush(YonaFrame *F) {
  /* The codegen's inline depth check (YonaRuntimeTryDepth) already
   * branches around the stores + push call when depth == 0. But
   * as a safety net, also check here. */
  if (YonaExc.Depth == 0) {
    F->DropCount = -1;
    return;
  }
  F->Prev = YonaCurrentFrame;
  YonaCurrentFrame = F;
}

void YonaRuntimeFramePop(YonaFrame *F) {
  if (!F)
    return; /* hot path: frame was never allocated (no try active) */
  YonaCurrentFrame = F->Prev;
}

void YonaRuntimeFrameTransfer(void *Ptr) {
  YonaFrame *F = YonaCurrentFrame;
  if (!F || !Ptr)
    return;
  for (int I = 0; I < F->DropCount; I++) {
    if (F->Drops[I] == Ptr) {
      F->Drops[I] = NULL;
      return;
    }
  }
}

static void unwindFramesTo(YonaFrame *Stop) {
  while (YonaCurrentFrame && YonaCurrentFrame != Stop) {
    YonaFrame *F = YonaCurrentFrame;
    for (int I = 0; I < F->DropCount; I++) {
      void *P = F->Drops[I];
      if (P) {
        F->Drops[I] = NULL;
        YonaRuntimeRelease(P);
      }
    }
    YonaCurrentFrame = F->Prev;
  }
}

// YonaRuntimeTryBegin: push a jmp_buf slot, return pointer to it.
// The caller must call YONA_SJLJ_SETJMP / llvm.eh.sjlj.setjmp on the returned
// pointer directly (setjmp must execute in the caller's stack frame). Buffer
// is laid out as void*[5]; see yona_sjlj_buf_t above.
void *YonaRuntimeTryBegin(void) {
  if (YonaExc.Depth >= YONA_MAX_TRY_DEPTH) {
    fprintf(stderr, "Fatal: try/catch nesting depth exceeded\n");
    abort();
  }
  YonaExc.SavedFrame[YonaExc.Depth] = YonaCurrentFrame;
  YonaTryDepth = YonaExc.Depth + 1;
  return &YonaExc.Buf[YonaExc.Depth++];
}

void YonaRuntimeTryEnd(void) {
  if (YonaExc.Depth > 0)
    YonaExc.Depth--;
  YonaTryDepth = YonaExc.Depth;
}

void YonaRuntimePrintStackTrace(void) {
#if defined(__linux__) || defined(__APPLE__)
  void *frames[64];
  int n = backtrace(frames, 64);
  char **syms = backtrace_symbols(frames, n);
  if (syms) {
    fprintf(stderr, "Stack trace:\n");
    for (int i = 2; i < n; i++)
      fprintf(stderr, "  %s\n", syms[i]);
    free(syms);
  }
#elif defined(_WIN32)
  void *Frames[64];
  HANDLE Process = GetCurrentProcess();
  SymInitialize(Process, NULL, TRUE);
  WORD N = CaptureStackBackTrace(2, 62, Frames, NULL);
  fprintf(stderr, "Stack trace:\n");
  SYMBOL_INFO *Sym = (SYMBOL_INFO *)calloc(sizeof(SYMBOL_INFO) + 256, 1);
  Sym->MaxNameLen = 255;
  Sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  for (WORD I = 0; I < N; I++) {
    SymFromAddr(Process, (DWORD64)Frames[I], 0, Sym);
    fprintf(stderr, "  %s\n", Sym->Name);
  }
  free(Sym);
#endif
}

static void raiseCurrent(void) {
  if (YonaExc.Depth == 0) {
    fprintf(stderr, "Unhandled exception: \"%s\"\n",
            YonaExc.Current.Message ? YonaExc.Current.Message : "");
    YonaRuntimePrintStackTrace();
    abort();
  }
  YonaExc.Depth--;
  /* Phase 3: unwind owned-heap frames before longjmp blows past
   * their function-exit cleanups. */
  unwindFramesTo(YonaExc.SavedFrame[YonaExc.Depth]);
  /* Task-group bump arenas: free wholesale for scopes being torn past. */
  unwindTaskGroupArenasTo(YonaExc.Depth);
#if defined(__clang__) || defined(__GNUC__)
  yonaSjLjLongJump(&YonaExc.Buf[YonaExc.Depth]);
#else
  /* MSVC build of yona_lib.dll is loaded only by yonac.exe (compiler driver),
   * which never executes user IR — try/catch unwinding is dead code there.
   * User programs always link against the clang-built runtime where the
   * __builtin_* path is taken. Abort if we somehow reach here under MSVC. */
  fprintf(stderr, "Fatal: YonaRuntimeRaise called in MSVC-built runtime\n");
  abort();
#endif
}

void YonaRuntimeRaiseOwned(int64_t Symbol, const char *Payload, void *Owner) {
  /* Replacing an exception without first consuming it would otherwise orphan
   * the previous owner. This is also a defensive guard for native callers. */
  if (YonaExc.Current.Owner && YonaExc.Current.Owner != Owner)
    YonaRuntimeRelease(YonaExc.Current.Owner);
  YonaExc.Current.Symbol = Symbol;
  YonaExc.Current.Message = Payload;
  YonaExc.Current.Owner = Owner;
  raiseCurrent();
}

void YonaRuntimeRaise(int64_t Symbol, const char *Message) {
  char *OwnedMessage = NULL;
  if (Message) {
    const size_t Length = strlen(Message);
    OwnedMessage =
        (char *)YonaRuntimeAllocateStringWithLength(Length + 1, Length);
    if (OwnedMessage) {
      memcpy(OwnedMessage, Message, Length);
      OwnedMessage[Length] = '\0';
    }
  }
  YonaRuntimeRaiseOwned(Symbol, OwnedMessage, OwnedMessage);
}

void YonaRuntimeReraise(void) { raiseCurrent(); }

int64_t YonaRuntimeGetExceptionSymbol(void) { return YonaExc.Current.Symbol; }

const char *YonaRuntimeGetExceptionMessage(void) {
  return YonaExc.Current.Message;
}

void *YonaRuntimeTakeExceptionOwner(void) {
  void *Owner = YonaExc.Current.Owner;
  YonaExc.Current.Owner = NULL;
  return Owner;
}

void YonaRuntimeConsumeExceptionOwner(int64_t PayloadIsHeap) {
  void *Owner = YonaRuntimeTakeExceptionOwner();
  if (Owner && PayloadIsHeap && YonaExc.Current.Message)
    YonaRuntimeRetain((void *)YonaExc.Current.Message);
  YonaRuntimeRelease(Owner);
}

/* Forward declarations for runtime functions used by shims */
int64_t *YonaRuntimeSequenceAllocate(int64_t Count);
int64_t *YonaRuntimeSequenceTail(int64_t *Seq);
