/* ===== Partial Application (Closures) ===== */
/* A closure captures a function pointer and one partial argument.
 * When called with the remaining argument, it invokes the original
 * function with both args.
 *
 * For functions with arity > 2, closures chain:
 * add 1 → closure(add, 1) → call with 2 → closure(add_1, 2) → call with 3 → add(1,2,3)
 */

typedef struct {
    int64_t (*fn2)(int64_t, int64_t);  /* original 2-arg function */
    int64_t captured;                   /* first captured argument */
} yona_closure2_t;

typedef struct {
    int64_t (*fn3)(int64_t, int64_t, int64_t);
    int64_t captured1;
    int64_t captured2;
} yona_closure3_t;

/* Apply a 2-arg closure: closure was created from fn(captured, ?), now called with arg */
int64_t yona_rt_closure2_apply(int64_t closure_ptr, int64_t arg) {
    yona_closure2_t* c = (yona_closure2_t*)(void*)closure_ptr;
    int64_t result = c->fn2(c->captured, arg);
    yona_rt_rc_dec(c);
    return result;
}

/* Create a closure from a 2-arg function with 1 captured arg */
int64_t yona_rt_closure2_create(int64_t (*fn)(int64_t, int64_t), int64_t captured) {
    yona_closure2_t* c = (yona_closure2_t*)rc_alloc(RC_TYPE_CLOSURE, sizeof(yona_closure2_t));
    c->fn2 = fn;
    c->captured = captured;
    return (int64_t)(void*)c;
}

/* ===== General Closures (env-passing) ===== */
/* Layout: int64_t array
 * [fn_ptr, ret_type, arity, num_captures, heap_mask, borrow_mask, cap0, ...]
 * The function takes (void* env, args...) where env is the closure itself.
 * Slot 0: function pointer (as int64_t)
 * Slot 1: return CType tag (INT=0, FLOAT=1, ..., ADT=12)
 * Slot 2: number of user arguments (excluding env)
 * Slot 3: number of captures
 * Slot 4: heap_mask — bitmask of which captures are heap-typed (for recursive rc_dec)
 * Slot 5: borrow_mask — bit i is set when user parameter i is borrowed
 * Captures are stored starting at index 6.
 */

#define CLOSURE_HDR_SIZE 6

void* yona_rt_closure_create(void* fn_ptr, int64_t ret_type, int64_t arity, int64_t num_captures) {
    int64_t* closure = (int64_t*)rc_alloc(RC_TYPE_CLOSURE, (CLOSURE_HDR_SIZE + num_captures) * sizeof(int64_t));
    closure[0] = (int64_t)(intptr_t)fn_ptr;
    closure[1] = ret_type;
    closure[2] = arity;
    closure[3] = num_captures;
    closure[4] = 0; /* heap_mask — set by codegen via closure_set_heap_mask */
    closure[5] = 0; /* borrow_mask — owned parameters are the safe default */
    return closure;
}

void yona_rt_closure_set_heap_mask(void* closure, int64_t mask) {
    ((int64_t*)closure)[4] = mask;
}

void yona_rt_closure_set_borrow_mask(void* closure, int64_t mask) {
    ((int64_t*)closure)[5] = mask;
}

void yona_rt_closure_set_cap(void* closure, int64_t idx, int64_t val) {
    ((int64_t*)closure)[CLOSURE_HDR_SIZE + idx] = val;
}

int64_t yona_rt_closure_get_cap(void* closure, int64_t idx) {
    return ((int64_t*)closure)[CLOSURE_HDR_SIZE + idx];
}
