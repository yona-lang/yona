package yatta.runtime;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;

public class DictTest {
  private static final int N = 1 << 20;
  private static final int M = 1 << 12;
  private static final long SEED = 1234567890L;

  @Test
  public void testAddLookup() {
    Dict dict = Dict.empty(Murmur3.INSTANCE, SEED);
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

  @Test
  public void testAddOverwrite() {
    Dict dict = Dict.empty(Murmur3.INSTANCE, SEED);
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

  @Test
  public void testRemoveLookup() {
    Dict dict = Dict.empty(Murmur3.INSTANCE, SEED);
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

  @Test
  public void testEqualityAndHash() {
    Dict fst = Dict.empty(Murmur3.INSTANCE, SEED);
    Dict snd = Dict.empty(Murmur3.INSTANCE, SEED);
    for (int i = 0; i < M; i++) {
      Assertions.assertEquals(fst, snd);
      Assertions.assertEquals(fst.murmur3Hash(SEED), snd.murmur3Hash(SEED));
      fst = fst.add(new K(i), i);
      assertNotEquals(fst, snd);
      snd = snd.add(new K(i), i);
    }
  }

  @Test
  public void testKeySet() {
    Dict dict = Dict.empty(Murmur3.INSTANCE, SEED);
    Set set = Set.empty(Murmur3.INSTANCE, SEED);
    for (int i = 0; i < M; i++) {
      dict.add(new K(i), "");
      set.add(new K(i));
    }
    assertEquals(set, dict.keys());
  }

  private static final class K {
    final long value;
    final int hash;

    K(final long value) {
      this.value = value;
      this.hash = (int) value & 0x3ff;
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
      return hash;
    }
  }
}
