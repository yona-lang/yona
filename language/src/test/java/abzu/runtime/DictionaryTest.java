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
}
