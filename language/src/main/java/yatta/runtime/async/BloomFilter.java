package yatta.runtime.async;

import java.lang.invoke.VarHandle;

import static java.lang.Math.*;

final class BloomFilter {
  private static final double LOG2 = log(2);
  private static final double C = -1 * Long.SIZE * LOG2 * LOG2;

  private BloomFilter() {}

  static int lengthFor(final int insertions, final double falsePositiveProbability) {
    return (int) ceil(insertions * log(falsePositiveProbability) / C);
  }

  static int hashesFor(final int insertions, final int length) {
    return (int) max(1, round((double) length * 64 / insertions * LOG2));
  }

  static void add(final long[] data, final VarHandle handle, final int bitsPerHash, final long hash) {
    final long bits = data.length * Long.SIZE;
    int mix;
    long bit;
    long mask;
    int idx;
    for (int i = 1; i <= bitsPerHash; i++) {
      mix = mix(hash, i);
      bit = mix % bits;
      mask = 1L << bit;
      idx = (int) (bit >>> 6);
      long expected;
      long updated;
      do {
        expected = (long) handle.getVolatile(data, idx);
        updated = expected | mask;
      } while (!handle.compareAndSet(data, idx, expected, updated));
    }
  }

  static boolean mightContain(final long[] data, final VarHandle handle, final int bitsPerHash, final long hash) {
    final long bits = data.length * Long.SIZE;
    int mix;
    long bit;
    long mask;
    int idx;
    for (int i = 1; i <= bitsPerHash; i++) {
      mix = mix(hash, i);
      bit = mix % bits;
      mask = 1L << bit;
      idx = (int) (bit >>> 6);
      if (((long) handle.getVolatile(data, idx) & mask) == 0) {
        return false;
      }
    }
    return true;
  }

  private static int mix(final long hash, final int i) {
    final int mixed = (int) hash + (i * (int) (hash >>> 32));
    return mixed >= 0 ? mixed : ~mixed;
  }
}
