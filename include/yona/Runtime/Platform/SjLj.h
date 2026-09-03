/*
 * SJLJ buffer compatible with llvm.eh.sjlj.setjmp / codegen try/catch.
 *
 * Layout is the five-word GCC/LLVM contract: slot 0 = FP, slot 1 = resume IP,
 * slot 2 = SP, slots 3–4 reserved. YonaRuntimeRaise longjmps into the same
 * buffer that codegen or a worker setjmp'd.
 *
 * Clang 22+ rejects __builtin_setjmp/longjmp on AArch64 ("not supported for
 * the current target"). On that target we use inline asm that matches the
 * same three slots codegen already stores before llvm.eh.sjlj.setjmp.
 *
 * setjmp must expand in the caller's frame — never wrap it in a function.
 */

#ifndef YONA_RUNTIME_PLATFORM_SJLJ_H
#define YONA_RUNTIME_PLATFORM_SJLJ_H

#include <stdint.h>

#if defined(__aarch64__)

/* AArch64 longjmp bypasses every intervening function epilogue. In addition
 * to FP/SP/PC, preserve the AAPCS64 callee-saved GPRs x19-x28 and SIMD
 * registers d8-d15, otherwise values live across a Yona `try` can be read
 * from registers clobbered by the throwing native/runtime call. */
typedef struct {
  void *Frame;
  void *ResumePc;
  void *Stack;
  uint64_t GeneralPurpose[10];
  uint64_t FloatingPoint[8];
} YonaSjLjBufT;

#define YONA_SJLJ_SETJMP(Buffer)                                               \
  __extension__({                                                              \
    int YonaSjLjResult;                                                        \
    YonaSjLjBufT *YonaSjLjBuffer = (YonaSjLjBufT *)(Buffer);                   \
    __asm__ volatile("str x29, [%1]\n\t"                                       \
                     "adr x2, 1f\n\t"                                          \
                     "str x2, [%1, #8]\n\t"                                    \
                     "mov x2, sp\n\t"                                          \
                     "str x2, [%1, #16]\n\t"                                   \
                     "stp x19, x20, [%1, #24]\n\t"                             \
                     "stp x21, x22, [%1, #40]\n\t"                             \
                     "stp x23, x24, [%1, #56]\n\t"                             \
                     "stp x25, x26, [%1, #72]\n\t"                             \
                     "stp x27, x28, [%1, #88]\n\t"                             \
                     "str d8, [%1, #104]\n\t"                                  \
                     "str d9, [%1, #112]\n\t"                                  \
                     "str d10, [%1, #120]\n\t"                                 \
                     "str d11, [%1, #128]\n\t"                                 \
                     "str d12, [%1, #136]\n\t"                                 \
                     "str d13, [%1, #144]\n\t"                                 \
                     "str d14, [%1, #152]\n\t"                                 \
                     "str d15, [%1, #160]\n\t"                                 \
                     "mov %w0, #0\n\t"                                         \
                     "b 2f\n\t"                                                \
                     "1:\n\t"                                                  \
                     "mov %w0, #1\n\t"                                         \
                     "2:"                                                      \
                     : "=&r"(YonaSjLjResult)                                   \
                     : "r"(YonaSjLjBuffer)                                     \
                     : "x2", "x19", "x20", "x21", "x22", "x23", "x24", "x25",  \
                       "x26", "x27", "x28", "v8", "v9", "v10", "v11", "v12",   \
                       "v13", "v14", "v15", "memory");                         \
    YonaSjLjResult;                                                            \
  })

__attribute__((noreturn, always_inline)) static inline void
yonaSjLjLongJump(void *Buffer) {
  YonaSjLjBufT *YonaSjLjBuffer = (YonaSjLjBufT *)Buffer;
  __asm__ volatile("ldr d8, [%0, #104]\n\t"
                   "ldr d9, [%0, #112]\n\t"
                   "ldr d10, [%0, #120]\n\t"
                   "ldr d11, [%0, #128]\n\t"
                   "ldr d12, [%0, #136]\n\t"
                   "ldr d13, [%0, #144]\n\t"
                   "ldr d14, [%0, #152]\n\t"
                   "ldr d15, [%0, #160]\n\t"
                   "ldp x19, x20, [%0, #24]\n\t"
                   "ldp x21, x22, [%0, #40]\n\t"
                   "ldp x23, x24, [%0, #56]\n\t"
                   "ldp x25, x26, [%0, #72]\n\t"
                   "ldp x27, x28, [%0, #88]\n\t"
                   "ldr x29, [%0]\n\t"
                   "ldr x2, [%0, #16]\n\t"
                   "mov sp, x2\n\t"
                   "ldr x2, [%0, #8]\n\t"
                   "br x2"
                   :
                   : "r"(YonaSjLjBuffer)
                   : "x2", "memory");
  __builtin_unreachable();
}

#elif defined(__clang__) || defined(__GNUC__)

typedef void *YonaSjLjBufT[5];

#define YONA_SJLJ_SETJMP(Buffer) __builtin_setjmp((void **)(Buffer))

/* GCC errors on always_inline + __builtin_longjmp ("can never be inlined
 * because it uses setjmp-longjmp exception handling"). Clang still inlines. */
#if defined(__clang__)
__attribute__((noreturn, always_inline))
#else
__attribute__((noreturn))
#endif
static inline void yonaSjLjLongJump(void *Buffer) {
  __builtin_longjmp((void **)Buffer, 1);
}

#else

#error "yona SJLJ requires Clang/GCC builtins or AArch64 inline asm"

#endif

#endif /* YONA_RUNTIME_PLATFORM_SJLJ_H */
