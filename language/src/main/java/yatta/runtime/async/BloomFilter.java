package yatta.runtime.async;

import static java.lang.Math.*;

final class BloomFilter {
  private static final double LOG2 = log(2);
  private static final double C = -1 * Long.SIZE * LOG2 * LOG2;

  private BloomFilter() {}

  static int lengthFor(final int insertions, final double falsePositiveProbability) {
    return (int) ceil(insertions * log(falsePositiveProbability) / C);
  }

  static int hashesFor(final int insertions, final int length) {
    return (int) max(1, round((double) length * Long.SIZE / insertions * LOG2));
  }

  static void add(final long[] data, final int hashes, final long value) {
    final long bits = data.length * Long.SIZE;
    int mix;
    long bit;
    long mask;
    int idx;
    for (int i = 1; i <= hashes; i++) {
      mix = mix(value, i);
      bit = mix % bits;
      mask = 1L << bit;
      idx = (int) (bit >>> 6);
      data[idx] |= mask;
    }
  }

  static boolean mightContain(final long[] data, final int hashes, final long value) {
    final long bits = data.length * Long.SIZE;
    int mix;
    long bit;
    long mask;
    int idx;
    for (int i = 1; i <= hashes; i++) {
      mix = mix(value, i);
      bit = mix % bits;
      mask = 1L << bit;
      idx = (int) (bit >>> 6);
      if ((data[idx] & mask) == 0) {
        return false;
      }
    }
    return true;
  }

  static boolean mightIntersect(final long[] fst, final long[] snd, final int hashes) {
    int intersections = 0;
    for (int i = 0; i < fst.length; i++) {
      intersections += Long.bitCount(fst[i] & snd[i]);
      if (intersections >= hashes) {
        return true;
      }
    }
    return false;
  }

  private static int mix(final long hash, final int i) {
    final int mixed = (int) hash + (i * (int) (hash >>> 32));
    return mixed >= 0 ? mixed : ~mixed;
  }
}
