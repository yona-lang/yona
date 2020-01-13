package yatta.runtime;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class SetTest {

  private static final int N = 1 << 18;
  private static final int M = 1 << 12;
  private static final long SEED = 0L;
  
  @Test
  public void testAddContains() {
    Set set = Set.empty(Murmur3.INSTANCE, SEED);
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

  @Test
  public void testRemoveContains() {
    Set set = Set.empty(Murmur3.INSTANCE, SEED);
    for (int i = 0; i < N; i++) {
      set = set.add(new O(i));
      assertTrue(set.contains(new O(i)));
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        set = set.remove(new O(i));
        assertFalse(set.contains(new O(i)));
      }
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        assertFalse(set.contains(new O(i)));
      } else {
        assertTrue(set.contains(new O(i)));
        set = set.remove(new O(i));
      }
    }
    for (int i = 0; i < N; i++) {
      assertFalse(set.contains(new O(i)));
    }
  }

  @Test
  public void testEqualityAndHash() {
    Set fst = Set.empty(Murmur3.INSTANCE, SEED);
    Set snd = Set.empty(Murmur3.INSTANCE, SEED);
    for (int i = 0; i < M; i++) {
      assertEquals(fst, snd);
      assertEquals(fst.murmur3Hash(SEED), snd.murmur3Hash(SEED));
      fst = fst.add(new O(i));
      assertNotEquals(fst, snd);
      snd = snd.add(new O(i));
    }
  }

  @Test
  public void testContains() {
    Set set = Set.set(1l, 2l);
    assertTrue(set.contains(1l));
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
    final int hash;

    O(final long value) {
      this.value = value;
      this.hash = (int) value & 0x3ff;
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
      return hash;
    }
  }
}
