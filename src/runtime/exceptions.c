#include "yona/runtime/sjlj.h"

/* ===== Exception handling (setjmp/longjmp) ===== */

#define YONA_MAX_TRY_DEPTH 256

typedef struct {
    int64_t symbol;
    const char* message;
} yona_exception_t;

/* ===== Perceus phase 3: frame-scoped heap cleanup on unwind =====
 *
 * Each user-defined function with heap params allocates a
 * `yona_frame_t` on its own stack (via codegen alloca) and chains it
 * into the thread-local `yona_current_frame`. On normal exit the frame
 * is popped and cleanup happens via the codegen's function-exit rc_dec
 * loop. On raise, yona_rt_raise walks frames from the current one back
 * to the frame that was active when the surrounding try block ran
 * setjmp, rc_dec'ing any drops that haven't been transferred.
 *
 * Codegen emits:
 *   - alloca yona_frame_t
 *   - fills f->drops[0..N-1] with heap-typed param pointers, sets
 *     f->drop_count = N
 *   - calls yona_rt_frame_push(f) after entry-time rc setup
 *   - before returning normally, calls yona_rt_frame_pop(f)
 *   - at each transfer site (single-use SEQ/SET/DICT arg handed to
 *     a callee), calls yona_rt_frame_transfer(ptr) to NULL that slot
 *     so unwind won't double-dec something the callee now owns.
 *
 * The type tag we feed to the destructor lives in the RC header at
 * `ptr - 16`; `yona_rt_rc_dec` reads it, so the frame only needs to
 * hold raw pointers.
 */

#define YONA_MAX_FRAME_DROPS 16

typedef struct yona_frame {
    struct yona_frame* prev;
    int drop_count;
    int _pad;
    void* drops[YONA_MAX_FRAME_DROPS];
} yona_frame_t;

_Thread_local yona_frame_t* yona_current_frame = NULL;

/* SJLJ jmp_buf: 5 pointers per LLVM's llvm.eh.sjlj.setjmp / __builtin_setjmp
 * contract (frame, pc, sp, plus 2 target-reserved slots). Both worker threads
 * and codegen-emitted try/catch save into the same slots, so the runtime longjmp
 * (yona_sjlj_longjmp) is layout-compatible with both. We do NOT use the C runtime's
 * setjmp/longjmp here: on Windows MSVC, setjmp uses SEH-based unwinding that walks
 * the SEH chain on longjmp and crashes when we longjmp from a worker frame back into
 * codegen-emitted main (which has no SEH metadata). yona_sjlj_* save only FP/SP/IP,
 * never touch the SEH chain, and match llvm.eh.sjlj.setjmp on every target
 * (AArch64 uses inline asm because Clang rejects __builtin_setjmp there). */
typedef void* yona_sjlj_buf_t[5];

typedef struct {
    yona_sjlj_buf_t buf[YONA_MAX_TRY_DEPTH];
    int depth;
    yona_exception_t current;
    yona_frame_t* saved_frame[YONA_MAX_TRY_DEPTH];
} yona_exc_state_t;

static _Thread_local yona_exc_state_t yona_exc = { .depth = 0 };

/* Exposed as a TLS global for zero-overhead depth checks from codegen.
 * The codegen declares this as a thread_local i32 and loads it inline
 * (no function call) to branch around frame setup. */
_Thread_local int yona_try_depth = 0;

int yona_rt_try_depth(void) { return yona_try_depth; }

extern void yona_rt_rc_dec(void* ptr);
extern void yona_rt_group_end(void* g);

/* Task-group arena bindings: wholesale-free bump memory on raise unwind.
 * Each push records the task group and yona_try_depth at entry; raise
 * destroys arenas for bindings deeper than the catch we're jumping to. */
#define YONA_MAX_GROUP_ARENA_BIND 32
typedef struct {
    void* group; /* yona_task_group_t* */
    int try_depth;
} yona_group_arena_bind_t;

static _Thread_local yona_group_arena_bind_t yona_group_arena_stack[YONA_MAX_GROUP_ARENA_BIND];
static _Thread_local int yona_group_arena_sp = 0;

void yona_rt_group_arena_bind_push(void* group) {
    if (yona_group_arena_sp >= YONA_MAX_GROUP_ARENA_BIND) {
        fprintf(stderr, "Fatal: task-group arena bind stack overflow\n");
        abort();
    }
    yona_group_arena_stack[yona_group_arena_sp].group = group;
    yona_group_arena_stack[yona_group_arena_sp].try_depth = yona_try_depth;
    yona_group_arena_sp++;
}

void yona_rt_group_arena_bind_pop(void) {
    if (yona_group_arena_sp <= 0) return;
    yona_group_arena_sp--;
}

static void yona_rt_group_arena_unwind_to(int target_try_depth) {
    while (yona_group_arena_sp > 0) {
        yona_group_arena_bind_t* top = &yona_group_arena_stack[yona_group_arena_sp - 1];
        if (top->try_depth <= target_try_depth) break;
        void* g = top->group;
        yona_group_arena_sp--;
        /* Full group teardown (arena + mutex + free), matching codegen's
         * normal group_end path — must run before longjmp skips it. */
        yona_rt_group_end(g);
    }
}

void yona_rt_frame_push(yona_frame_t* f) {
    /* The codegen's inline depth check (yona_rt_try_depth) already
     * branches around the stores + push call when depth == 0. But
     * as a safety net, also check here. */
    if (yona_exc.depth == 0) {
        f->drop_count = -1;
        return;
    }
    f->prev = yona_current_frame;
    yona_current_frame = f;
}

void yona_rt_frame_pop(yona_frame_t* f) {
    if (!f) return;  /* hot path: frame was never allocated (no try active) */
    yona_current_frame = f->prev;
}

void yona_rt_frame_transfer(void* ptr) {
    yona_frame_t* f = yona_current_frame;
    if (!f || !ptr) return;
    for (int i = 0; i < f->drop_count; i++) {
        if (f->drops[i] == ptr) { f->drops[i] = NULL; return; }
    }
}

static void yona_rt_unwind_frames_to(yona_frame_t* stop) {
    while (yona_current_frame && yona_current_frame != stop) {
        yona_frame_t* f = yona_current_frame;
        for (int i = 0; i < f->drop_count; i++) {
            void* p = f->drops[i];
            if (p) {
                f->drops[i] = NULL;
                yona_rt_rc_dec(p);
            }
        }
        yona_current_frame = f->prev;
    }
}

// yona_rt_try_push: push a jmp_buf slot, return pointer to it.
// The caller must call yona_sjlj_setjmp / llvm.eh.sjlj.setjmp on the returned
// pointer directly (setjmp must execute in the caller's stack frame). Buffer
// is laid out as void*[5]; see yona_sjlj_buf_t above.
void* yona_rt_try_push(void) {
    if (yona_exc.depth >= YONA_MAX_TRY_DEPTH) {
        fprintf(stderr, "Fatal: try/catch nesting depth exceeded\n");
        abort();
    }
    yona_exc.saved_frame[yona_exc.depth] = yona_current_frame;
    yona_try_depth = yona_exc.depth + 1;
    return yona_exc.buf[yona_exc.depth++];
}

void yona_rt_try_end(void) {
    if (yona_exc.depth > 0) yona_exc.depth--;
    yona_try_depth = yona_exc.depth;
}

void yona_rt_print_stacktrace(void) {
#if defined(__linux__) || defined(__APPLE__)
    void* frames[64];
    int n = backtrace(frames, 64);
    char** syms = backtrace_symbols(frames, n);
    if (syms) {
        fprintf(stderr, "Stack trace:\n");
        for (int i = 2; i < n; i++)
            fprintf(stderr, "  %s\n", syms[i]);
        free(syms);
    }
#elif defined(_WIN32)
    void* frames[64];
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    WORD n = CaptureStackBackTrace(2, 62, frames, NULL);
    fprintf(stderr, "Stack trace:\n");
    SYMBOL_INFO* sym = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256, 1);
    sym->MaxNameLen = 255;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    for (WORD i = 0; i < n; i++) {
        SymFromAddr(process, (DWORD64)frames[i], 0, sym);
        fprintf(stderr, "  %s\n", sym->Name);
    }
    free(sym);
#endif
}

void yona_rt_raise(int64_t symbol, const char* message) {
    if (yona_exc.depth == 0) {
        /* For display: message contains "symbol_name\0actual_message" if from codegen,
           but since we get them separately, just print both */
        fprintf(stderr, "Unhandled exception: \"%s\"\n", message ? message : "");
        yona_rt_print_stacktrace();
        abort();
    }
    yona_exc.current.symbol = symbol;
    yona_exc.current.message = message;
    yona_exc.depth--;
    /* Phase 3: unwind owned-heap frames before longjmp blows past
     * their function-exit cleanups. */
    yona_rt_unwind_frames_to(yona_exc.saved_frame[yona_exc.depth]);
    /* Task-group bump arenas: free wholesale for scopes being torn past. */
    yona_rt_group_arena_unwind_to(yona_exc.depth);
#if defined(__clang__) || defined(__GNUC__)
    yona_sjlj_longjmp(yona_exc.buf[yona_exc.depth]);
#else
    /* MSVC build of yona_lib.dll is loaded only by yonac.exe (compiler driver),
     * which never executes user IR — try/catch unwinding is dead code there.
     * User programs always link against the clang-built runtime where the
     * __builtin_* path is taken. Abort if we somehow reach here under MSVC. */
    fprintf(stderr, "Fatal: yona_rt_raise called in MSVC-built runtime\n");
    abort();
#endif
}

int64_t yona_rt_get_exception_symbol(void) {
    return yona_exc.current.symbol;
}

const char* yona_rt_get_exception_message(void) {
    return yona_exc.current.message;
}

/* Forward declarations for runtime functions used by shims */
int64_t* yona_rt_seq_alloc(int64_t count);
int64_t* yona_rt_seq_tail(int64_t* seq);
