package yona.runtime.stm;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

// bloom filter represented as an array of longs
// array length must be a power of two and no greater than 16777216
// number of hash function to compute must stay consistent
final class BloomFilters {
    static final VarHandle HANDLE = MethodHandles.arrayElementVarHandle(long[].class);
    static final double LOG_2 = Math.log(2);

    private BloomFilters() {}

    // encode supplied 64-bit hash into the filter
    // n - number of hash functions to compute, must be positive
    public static void encode(final long[] filter, final long hash64, final int n) {
        final int bitSizeMask = (filter.length << 6) - 1;
        final int fst = (int) hash64;
        final int snd = (int) (hash64 >>> 32);
        int hash32 = fst;
        for (int i = 0; i < n; i++) {
            hash32 += snd;
            final int bitIdx = (hash32 < 0 ? ~hash32 : hash32) & bitSizeMask;
            final int longIdx = bitIdx >>> 6;
            final long mask = 1L << bitIdx;
            filter[longIdx] |= mask;
        }
    }

    // encode supplied 64-bit hash into the filter using GET_AND_BITWISE_OR_RELEASE access mode
    // n - number of hash functions to compute, must be positive
    public static void encodeAtomically(final long[] filter, final long hash64, final int n) {
        final int bitSizeMask = (filter.length << 6) - 1;
        final int fst = (int) hash64;
        final int snd = (int) (hash64 >>> 32);
        int hash32 = fst;
        for (int i = 0; i < n; i++) {
            hash32 += snd;
            final int bitIdx = (hash32 < 0 ? ~hash32 : hash32) & bitSizeMask;
            final int longIdx = bitIdx >>> 6;
            final long mask = 1L << bitIdx;
            HANDLE.getAndBitwiseOrRelease(filter, longIdx, mask);
        }
    }

    // returns whether supplied 64-bit hash might have been previously encoded in this filter
    // n - number of hash functions to compute, must be positive
    public static boolean mayContain(final long[] filter, final long hash64, final int n) {
        final int bitSizeMask = (filter.length << 6) - 1;
        final int fst = (int) hash64;
        final int snd = (int) (hash64 >>> 32);
        int hash32 = fst;
        for (int i = 0; i < n; i++) {
            hash32 += snd;
            final int bitIdx = (hash32 < 0 ? ~hash32 : hash32) & bitSizeMask;
            final int longIdx = bitIdx >>> 6;
            final long mask = 1L << bitIdx;
            if ((filter[longIdx] & mask) == 0L) {
                return false;
            }
        }
        return true;
    }

    // returns the optimal size of the filter, in longs, rounded to the nearest power of two
    // items - estimated number of items to be encoded, must be positive
    // falsePositiveProbability must be positive
    public static int optimalLength(final int items, final double falsePositiveProbability) {
        return nextPowerOfTwo((int) (-items * Math.log(falsePositiveProbability) / (LOG_2 * LOG_2 * 64)));
    }

    // returns the optimal number of 32-bit hashes to compute per element
    // items - estimated number of items to be encoded, must be positive
    // length - filter length, in longs
    public static int optimalNumberOfHashFunctions(final int items, final int length) {
        return (int) Math.ceil(LOG_2 * (64.0D * length / items));
    }

    static int nextPowerOfTwo(final int value) {
        final int n = Integer.numberOfLeadingZeros(value - 1);
        return 1 << -n;
    }
}
