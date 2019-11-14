package yatta.runtime;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SetTest {

  private static final int N = 1 << 24;
  
  @Test
  public void testAddLookup() {
    Set set = Set.empty(Murmur3.INSTANCE, 0L);
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        set = set.add(i);
      }
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        assertTrue(set.contains(i));
      } else {
        assertFalse(set.contains(i));
      }
    }
  }

  @Test
  public void testRemoveLookup() {
    Set set = Set.empty(Murmur3.INSTANCE, 0L);
    for (int i = 0; i < N; i++) {
      set = set.add(i);
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        set = set.remove(i);
      }
    }
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0) {
        assertFalse(set.contains(i));
      } else {
        assertTrue(set.contains(i));
        set = set.remove(i);
      }
    }
    for (int i = 0; i < N; i++) {
      assertFalse(set.contains(i));
    }
  }

  @Test
  public void testCollision() {
    Set set = Set.empty(Murmur3.INSTANCE, 0L);
    O[] os = new O[2];
    for (int i = 0; i < os.length; i++) {
      os[i] = new O(i);
      set = set.add(os[i]);
    }
    for (O o : os) {
      assertTrue(set.contains(o));
    }
  }

  private static final class O {
    final int hash;

    O(int hash) {
      this.hash = hash;
    }

    @Override
    public int hashCode() {
      return hash;
    }
  }
}
