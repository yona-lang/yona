package yatta.runtime;

import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import static org.junit.Assert.assertEquals;
import static org.junit.jupiter.api.Assertions.*;

@Tag("slow")
public class SetTest {
  private static final int N = 1 << 24;
  private static final int M = 1 << 12;

  @ParameterizedTest
  @ValueSource(longs = {0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
  public void testAddContains(final long seed) {
    Set set = Set.empty(Murmur3.INSTANCE, seed);
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        set = set.add(new O(i));
        assertTrue(set.contains(new O(i)));
      } else {
        assertFalse(set.contains(new O(i)));
      }
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        assertTrue(set.contains(new O(i)));
      } else {
        assertFalse(set.contains(new O(i)));
      }
    }
  }

  @ParameterizedTest
  @ValueSource(longs = {0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
  public void testRemoveContains(final long seed) {
    Set set = Set.empty(Murmur3.INSTANCE, seed);
    for (int i = 0; i < N; i++) {
      set = set.add(new O(i));
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        set = set.remove(new O(i));
        assertFalse(set.contains(new O(i)));
      }
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 != 0) {
        assertTrue(set.contains(new O(i)));
        set = set.remove(new O(i));
      }
    }
    for (int i = 0; i < N; i++) {
      assertFalse(set.contains(new O(i)));
    }
  }

  @ParameterizedTest
  @ValueSource(longs = {0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
  public void testEqualityAndHash(final long seed) {
    Set fst = Set.empty(Murmur3.INSTANCE, seed);
    Set snd = Set.empty(Murmur3.INSTANCE, seed);
    for (int i = 0; i < M; i++) {
      assertEquals(fst, snd);
      assertEquals(fst.murmur3Hash(seed), snd.murmur3Hash(seed));
      fst = fst.add(new O(i));
      assertNotEquals(fst, snd);
      snd = snd.add(new O(i));
    }
  }

  @Test
  public void testCtor() {
    Set set = Set.set(1L, 2L);
    assertTrue(set.contains(1L));
  }

  @Test
  public void testCompareTo() {
    Set fst = Set.set(1, 2);
    Set snd = Set.set(1, 2, 3, 4);
    assertTrue(fst.compareTo(fst) == 0);  // equal sets
    assertTrue(fst.compareTo(snd) < 0);   // proper subset
    assertTrue(snd.compareTo(fst) > 0);   // proper supserset
  }

  private static final class O {
    final long value;

    O(final long value) {
      this.value = value;
    }

    @Override
    public boolean equals(Object o) {
      if (!(o instanceof O)) {
        return false;
      }
      final O that = (O) o;
      return this.value == that.value;
    }

    @Override
    public int hashCode() {
      return (int) value;
    }
  }

  @Test
  public void testSetCollector() {
    java.util.Set<Long> input = java.util.Set.of(1L, 2L, 3L);
    Set expected = Set.set(2L, 4L, 6L);
    Set result = input.stream().map(i -> i * 2).collect(Set.collect());

    assertEquals(expected.size(), result.size());
    assertEquals(expected, result);
  }
}
