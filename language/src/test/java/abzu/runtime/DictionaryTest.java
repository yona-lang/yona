package abzu.runtime;

import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class DictionaryTest {

  @Test
  public void testInjectLookup() {
    Dictionary dict = Dictionary.dictionary();
    for (int i = 0; i < Short.MAX_VALUE; i++) {
      if (i % 2 == 0) dict = dict.insert(i, i);
    }
    for (int i = 0; i < Short.MAX_VALUE; i++) {
      if (i % 2 == 0) assertEquals(i, dict.lookup(i));
      else assertEquals(Unit.INSTANCE, dict.lookup(i));
    }
  }

  @Test
  public void testRemoveLookup() {
    Dictionary dict = Dictionary.dictionary();
    for (int i = 0; i < Short.MAX_VALUE; i++) {
      dict = dict.insert(i, i);
    }
    for (int i = 0; i < Short.MAX_VALUE; i++) {
      if (i % 2 == 0) dict = dict.remove(i);
    }
    for (int i = 0; i < Short.MAX_VALUE; i++) {
      if (i % 2 == 0) assertEquals(Unit.INSTANCE, dict.lookup(i));
      else assertEquals(i, dict.lookup(i));
    }
  }

  @Test
  public void testCollision() {
    Dictionary dict = Dictionary.dictionary();
    O[] os = new O[2];
    for (int i = 0; i < os.length; i++) {
      os[i] = new O(i);
      dict = dict.insert(os[i], i);
    }
    for (int i = 0; i < os.length; i++) {
      assertEquals(i, dict.lookup(os[i]));
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
