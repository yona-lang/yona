package yatta.runtime;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import static org.junit.Assert.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;

@Tag("slow")
public class DictTest {
  private static final int N = 1 << 24;
  private static final int M = 1 << 12;

  @ParameterizedTest
  @ValueSource(longs = { 0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
  public void testAddLookup(final long seed) {
    Dict dict = Dict.empty(Murmur3.INSTANCE, seed);
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        dict = dict.add(new K(i), i);
        assertEquals(i, dict.lookup(new K(i)));
      } else {
        assertEquals(Unit.INSTANCE, dict.lookup(new K(i)));
      }
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        assertEquals(i, dict.lookup(new K(i)));
      } else {
        assertEquals(Unit.INSTANCE, dict.lookup(new K(i)));
      }
    }
  }

  @ParameterizedTest
  @ValueSource(longs = { 0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
  public void testAddOverwrite(final long seed) {
    Dict dict = Dict.empty(Murmur3.INSTANCE, seed);
    for (int i = 0; i < N; i++) {
      dict = dict.add(new K(i), "");
    }
    for (int i = 0; i < N; i++) {
      dict = dict.add(new K(i), i);
    }
    for (int i = 0; i < N; i++) {
      assertEquals(i, dict.lookup(new K(i)));
    }
  }

  @ParameterizedTest
  @ValueSource(longs = { 0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
  public void testRemoveLookup(final long seed) {
    Dict dict = Dict.empty(Murmur3.INSTANCE, seed);
    for (int i = 0; i < N; i++) {
      dict = dict.add(new K(i), i);
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        dict = dict.remove(new K(i));
        assertEquals(Unit.INSTANCE, dict.lookup(new K(i)));
      }
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 != 0) {
        assertEquals(i, dict.lookup(new K(i)));
        dict = dict.remove(new K(i));
      }
    }
    for (int i = 0; i < N; i++) {
      assertEquals(Unit.INSTANCE, dict.lookup(new K(i)));
    }
  }

  @ParameterizedTest
  @ValueSource(longs = { 0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
  public void testEqualityAndHash(final long seed) {
    Dict fst = Dict.empty(Murmur3.INSTANCE, seed);
    Dict snd = Dict.empty(Murmur3.INSTANCE, seed);
    for (int i = 0; i < M; i++) {
      Assertions.assertEquals(fst, snd);
      Assertions.assertEquals(fst.murmur3Hash(seed), snd.murmur3Hash(seed));
      fst = fst.add(new K(i), i);
      assertNotEquals(fst, snd);
      snd = snd.add(new K(i), i);
    }
  }

  @Test
  public void testKeySet() {
    Dict dict = Dict.empty(Murmur3.INSTANCE, 0L);
    Set set = Set.empty(Murmur3.INSTANCE, 0L);
    for (int i = 0; i < N; i++) {
      dict.add(new K(i), "");
      set.add(new K(i));
    }
    assertEquals(set, dict.keys());
  }

  private static final class K {
    final long value;

    K(final long value) {
      this.value = value;
    }

    @Override
    public boolean equals(Object o) {
      if (!(o instanceof K)) {
        return false;
      }
      final K that = (K) o;
      return this.value == that.value;
    }

    @Override
    public int hashCode() {
      return (int) value;
    }
  }
}
