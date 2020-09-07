package yona.runtime.stm;


import java.lang.reflect.Array;
import java.util.function.BiFunction;
import java.util.function.ToLongFunction;

// mutable linear probing hash set with upper limit on the number of probes
// array length must be a power of two
// does not support remove operation
final class LinearProbingHashSets {
    private LinearProbingHashSets() {}

    // when adding an element equal to the one already in (as far as supplied equality function is concerned), overwrite
    // caller has to always use the array returned by this function
    static <T> T[] add(T[] array, final T value, final BiFunction<? super T, ? super T, Boolean> equality, final ToLongFunction<? super T> hashing) {
        final int idx = index(hashing.applyAsLong(value), array.length);
        int probes = log2(array.length);
        // search until we run out of probes or reach end of the array:
        for (int i = idx; i < array.length; i++) {
            final T member = array[i];
            if (member == null || equality.apply(value, member)) {
                array[i] = value;
                return array;
            } else {
                if (--probes == 0) {
                    break;
                }
            }
        }
        array = grow(array, equality, hashing);
        return add(array, value, equality, hashing);
    }

    // returns value equal to the one supplied or null if none found
    static <T> T lookup(final T[] array, final T value, final BiFunction<? super T, ? super T, Boolean> equality, final ToLongFunction<? super T> hashing) {
        final int idx = index(hashing.applyAsLong(value), array.length);
        int probes = log2(array.length);
        // same, search until we run out of probes or reach end of the array:
        for (int i = idx; i < array.length; i++) {
            final T member = array[i];
            if (member == null) {
                // null means it's definitely not there because removal is not supported and thus there can be no holes:
                break;
            } else if (equality.apply(value, member)) {
                return member;
            } else {
                if (--probes == 0) {
                    break;
                }
            }
        }
        return null;
    }

    static int index(final long hash, final int len) {
        return (int) (hash & (len - 1));
    }

    @SuppressWarnings("unchecked")
    static <T> T[] grow(final T[] array, final BiFunction<? super T, ? super T, Boolean> equality, final ToLongFunction<? super T> hashing) {
        T[] result = (T[]) Array.newInstance(array.getClass().getComponentType(), array.length * 2);
        for (T value : array) {
            if (value != null) {
                result = add(result, value, equality, hashing);
            }
        }
        return result;
    }

    static int log2(final int n) {
        return 31 - Integer.numberOfLeadingZeros(n);
    }
}
