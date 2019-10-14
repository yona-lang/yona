package yatta.runtime;

import static java.lang.Long.rotateLeft;

public final class Hashing {
  static final long PRIME_0 = 5239883674171917637L;
  static final long PRIME_1 = 1267067482056083053L;
  static final long PRIME_2 = 2240059836128749097L;
  static final long PRIME_3 = 8463748566948153439L;
  static final long PRIME_4 = 6507735635434025711L;

  public static Hashing DEFAULT = new Hashing(0L);

  final long seed;
  final long hashTrue;
  final long hashFalse;

  Hashing(final long seed) {
    this.seed = seed;
    this.hashTrue = finalize(initHash(seed) + 1);
    this.hashFalse = finalize(initHash(seed));
  }

  public long hash(final Object o) {
    if (o instanceof Boolean) {
      final Boolean value = (Boolean) o;
      return value ? hashTrue : hashFalse;
    } else if (o instanceof Byte) {
      final Byte value = (Byte) o;
      return finalize(process1(initHash(seed) + 1, value));
    } else if (o instanceof Integer) {
      final Integer value = (Integer) o;
      return finalize(process4(initHash(seed) + 4, valueOf(value)));
    } else if (o instanceof Long) {
      final Long value = (Long) o;
      return finalize(process8(initHash(seed) + 8, valueOf(value)));
    } else if (o instanceof Double) {
      final Double value = (Double) o;
      return finalize(process8(initHash(seed) + 8, valueOf(value)));
    } else if (o instanceof Object[]) {
      final Object[] values = (Object[]) o;
      if (values.length >= 4) {
        long acc0 = init0(seed);
        long acc1 = init1(seed);
        long acc2 = init2(seed);
        long acc3 = init3(seed);
        int idx = 0;
        for (int remaining = values.length; remaining / 4 != 0; remaining -= 4) {
          acc0 = process(acc0, hash(values[idx]));
          acc1 = process(acc1, hash(values[idx + 1]));
          acc2 = process(acc2, hash(values[idx + 2]));
          acc3 = process(acc3, hash(values[idx + 3]));
          idx += 4;
        }
        long hash = initHash(acc0, acc1, acc2, acc3);
        hash += values.length;
        while (idx < values.length) {
          hash = process8(hash, hash(values[idx]));
          idx++;
        }
        return finalize(hash);
      } else {
        long hash = initHash(seed) + values.length;
        for (Object value : values) {
          hash = process8(hash, hash(value));
        }
        return finalize(hash);
      }
    } else {
      throw new AssertionError(); // TODO
    }
  }

  static long init0(final long seed) {
    return seed + PRIME_0 + PRIME_1;
  }

  static long init1(final long seed) {
    return seed + PRIME_1;
  }

  static long init2(final long seed) {
    return seed;
  }

  static long init3(final long seed) {
    return seed - PRIME_0;
  }

  static long process(long accumulator, final long value) {
    accumulator += value * PRIME_1;
    accumulator = rotateLeft(accumulator, 31);
    accumulator *= PRIME_0;
    return accumulator;
  }

  static long initHash(final long seed) {
    return seed + PRIME_4;
  }

  static long initHash(long value0, long value1, long value2, long value3) {
    value0 = rotateLeft(value0, 1);
    value1 = rotateLeft(value1, 7);
    value2 = rotateLeft(value2, 12);
    value3 = rotateLeft(value3, 18);
    long hash = value0 + value1 + value2 + value3;
    hash = accumulate(hash, value0);
    hash = accumulate(hash, value1);
    hash = accumulate(hash, value2);
    hash = accumulate(hash, value3);
    return hash;
  }

  static long accumulate(long hash, long value) {
    value *= PRIME_1;
    value = rotateLeft(value, 31);
    value *= PRIME_0;
    hash ^= value;
    hash *= PRIME_0;
    hash += PRIME_3;
    return hash;
  }

  static long valueOf(final double float64) {
    return Long.reverseBytes(Double.doubleToLongBits(float64));
  }

  static long valueOf(final long int64) {
    return Long.reverseBytes(int64);
  }

  static long valueOf(final int int32) {
    return 0xffffffffL & Integer.reverseBytes(int32);
  }

  static long process8(long hash, long value) {
    value *= PRIME_1;
    value = rotateLeft(value, 31);
    value *= PRIME_0;
    hash ^= value;
    hash = rotateLeft(hash, 27);
    hash *= PRIME_0;
    hash += PRIME_3;
    return hash;
  }

  static long process4(long hash, long value) {
    value *= PRIME_0;
    hash ^= value;
    hash = rotateLeft(hash, 23);
    hash *= PRIME_1;
    hash += PRIME_2;
    return hash;
  }

  static long process1(long hash, long value) {
    value *= PRIME_4;
    hash ^= value;
    hash = rotateLeft(hash, 11) * PRIME_0;
    return hash;
  }

  static long finalize(long value) {
    value ^= value >>> 33;
    value *= PRIME_1;
    value ^= value >>> 29;
    value *= PRIME_2;
    value ^= value >>> 32;
    return value;
  }
}
