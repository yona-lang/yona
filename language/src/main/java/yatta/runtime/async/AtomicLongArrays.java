package yatta.runtime.async;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

final class AtomicLongArrays {
  private static final VarHandle HANDLE = MethodHandles.arrayElementVarHandle(long[].class);

  private AtomicLongArrays() {}

  static long get(final long[] array, final int idx) {
    return (long) HANDLE.getVolatile(array, idx);
  }

  static boolean compareAndSet(final long[] array, final int idx, final long expected, final long updated) {
    return HANDLE.compareAndSet(array, idx, expected, updated);
  }
}
