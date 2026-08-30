#include "Runtime/Core/Internal.h"

#include <stdint.h>

/* ===== Partial Application (Closures) ===== */
/* A closure captures a function pointer and one partial argument.
 * When called with the remaining argument, it invokes the original
 * function with both args.
 *
 * For functions with arity > 2, closures chain:
 * add 1 → closure(add, 1) → call with 2 → closure(add_1, 2) → call with 3 →
 * add(1,2,3)
 */

typedef struct {
  int64_t (*Fn2)(int64_t, int64_t); /* original 2-arg function */
  int64_t Captured;                 /* first captured argument */
} YonaClosure2;

typedef struct {
  int64_t (*Fn3)(int64_t, int64_t, int64_t);
  int64_t Captured1;
  int64_t Captured2;
} YonaClosure3;

/* Apply a 2-arg closure: closure was created from fn(captured, ?), now called
 * with arg */
int64_t YonaRuntimeClosure2Apply(int64_t ClosurePtr, int64_t Arg) {
  YonaClosure2 *C = (YonaClosure2 *)(void *)ClosurePtr;
  int64_t Result = C->Fn2(C->Captured, Arg);
  YonaRuntimeRelease(C);
  return Result;
}

/* Create a closure from a 2-arg function with 1 captured arg */
int64_t YonaRuntimeClosure2Create(int64_t (*Fn)(int64_t, int64_t),
                                  int64_t Captured) {
  YonaClosure2 *C = (YonaClosure2 *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_CLOSURE, sizeof(YonaClosure2));
  C->Fn2 = Fn;
  C->Captured = Captured;
  return (int64_t)(void *)C;
}

/* ===== General Closures (env-passing) ===== */
/* Layout: int64_t array
 * [fn_ptr, ret_type, arity, num_captures, heap_mask, borrow_mask, cap0, ...]
 * The function takes (void* env, args...) where env is the closure itself.
 * Slot 0: function pointer (as int64_t)
 * Slot 1: return CType tag (INT=0, FLOAT=1, ..., ADT=12)
 * Slot 2: number of user arguments (excluding env)
 * Slot 3: number of captures
 * Slot 4: heap_mask — bitmask of which captures are heap-typed (for recursive
 * rc_dec) Slot 5: borrow_mask — bit i is set when user parameter i is borrowed
 * Captures are stored starting at index 6.
 */

#define YONA_CLOSURE_HDR_SIZE 6

void *YonaRuntimeClosureCreate(void *Function, int64_t ReturnType,
                               int64_t Arity, int64_t CaptureCount) {
  int64_t *Closure = (int64_t *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_CLOSURE,
      (YONA_CLOSURE_HDR_SIZE + CaptureCount) * sizeof(int64_t));
  Closure[0] = (int64_t)(intptr_t)Function;
  Closure[1] = ReturnType;
  Closure[2] = Arity;
  Closure[3] = CaptureCount;
  Closure[4] = 0; /* heap_mask — set by codegen via closure_set_heap_mask */
  Closure[5] = 0; /* borrow_mask — owned parameters are the safe default */
  return Closure;
}

void YonaRuntimeClosureSetHeapMask(void *Closure, int64_t Mask) {
  ((int64_t *)Closure)[4] = Mask;
}

void YonaRuntimeClosureSetBorrowMask(void *Closure, int64_t Mask) {
  ((int64_t *)Closure)[5] = Mask;
}

void YonaRuntimeClosureSetCapture(void *Closure, int64_t Index, int64_t Value) {
  ((int64_t *)Closure)[YONA_CLOSURE_HDR_SIZE + Index] = Value;
}

int64_t YonaRuntimeClosureGetCapture(void *Closure, int64_t Index) {
  return ((int64_t *)Closure)[YONA_CLOSURE_HDR_SIZE + Index];
}
