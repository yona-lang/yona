package yona.runtime.stm;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import java.util.function.BiFunction;
import java.util.function.ToLongFunction;

import static org.junit.Assert.assertEquals;

public class LinearProbingHashSetsTest {
    static final int N = 10000;

    static final BiFunction<Long, Long, Boolean> EQ = Long::equals;
    static final ToLongFunction<Long> HASH = Long::longValue;

    @ParameterizedTest
    @ValueSource(longs = {6L, 28L, 496L, 8128L, 33550336L, 8589869056L, 137438691328L, 2305843008139952128L})
    public void testAddLookup(final long seed) {
        final Set<Long> src = new HashSet<>();
        Long[] set = new Long[1];
        Random random = new Random(seed);
        for (int i = 0; i < N; i++) {
            final long value = random.nextLong();
            src.add(value);
            set = LinearProbingHashSets.add(set, value, EQ, HASH);
            for (Long l : src) {
                assertEquals(l, LinearProbingHashSets.lookup(set, l, EQ, HASH));
            }
        }
    }
}
