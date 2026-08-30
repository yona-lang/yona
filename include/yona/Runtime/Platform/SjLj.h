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

#if defined(__aarch64__)

#define YONA_SJLJ_SETJMP(Buffer)                                               \
  __extension__({                                                              \
    int YonaSjLjResult;                                                        \
    void **YonaSjLjBuffer = (void **)(Buffer);                                 \
    __asm__ volatile("str x29, [%1]\n\t"                                       \
                     "adr x2, 1f\n\t"                                          \
                     "str x2, [%1, #8]\n\t"                                    \
                     "mov x2, sp\n\t"                                          \
                     "str x2, [%1, #16]\n\t"                                   \
                     "mov %w0, #0\n\t"                                         \
                     "b 2f\n\t"                                                \
                     "1:\n\t"                                                  \
                     "mov %w0, #1\n\t"                                         \
                     "2:"                                                      \
                     : "=&r"(YonaSjLjResult)                                   \
                     : "r"(YonaSjLjBuffer)                                     \
                     : "x2", "memory");                                        \
    YonaSjLjResult;                                                            \
  })

__attribute__((noreturn, always_inline)) static inline void
yonaSjLjLongJump(void **Buffer) {
  __asm__ volatile("ldr x29, [%0]\n\t"
                   "ldr x2, [%0, #16]\n\t"
                   "mov sp, x2\n\t"
                   "ldr x2, [%0, #8]\n\t"
                   "br x2"
                   :
                   : "r"(Buffer)
                   : "x2", "memory");
  __builtin_unreachable();
}

#elif defined(__clang__) || defined(__GNUC__)

#define YONA_SJLJ_SETJMP(Buffer) __builtin_setjmp((void **)(Buffer))

/* GCC errors on always_inline + __builtin_longjmp ("can never be inlined
 * because it uses setjmp-longjmp exception handling"). Clang still inlines. */
#if defined(__clang__)
__attribute__((noreturn, always_inline))
#else
__attribute__((noreturn))
#endif
static inline void yonaSjLjLongJump(void **Buffer) {
  __builtin_longjmp(Buffer, 1);
}

#else

#error "yona SJLJ requires Clang/GCC builtins or AArch64 inline asm"

#endif

#endif /* YONA_RUNTIME_PLATFORM_SJLJ_H */
