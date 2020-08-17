package yona.runtime;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import yona.runtime.async.Promise;

import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.jupiter.api.Assertions.assertNotEquals;

@Tag("slow")
public class DictTest {
  private static final int N = 1 << 24;
  private static final int M = 1 << 12;

  @ParameterizedTest
  @ValueSource(longs = {0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
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
  @ValueSource(longs = {0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
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
  @ValueSource(longs = {0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
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
  @ValueSource(longs = {0L, 0xaaaaaaaaaaaaaaaaL, 0xffffffffffffffffL})
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

  @Test
  public void testInsertSameKey() {
    Dict dict = Dict.EMPTY.add(1, true).add(1, false);
    assertEquals(dict.lookup(1), false);
  }

  @Test
  public void testUnion() {
    Dict dict1 = Dict.EMPTY.add(1, 2);
    Dict dict2 = Dict.EMPTY.add(1, 3);
    assertEquals(3, dict1.union(dict2).lookup(1));
  }

  @Test
  public void testUnwrapPromises() {
    Promise eight = new Promise();
    Dict dict1 = Dict.EMPTY.add(1, 2).add(3, new Promise(4)).add(new Promise(5), 6).add(new Promise(7), eight);
    Object dict2 = dict1.unwrapPromises(null);
    assertTrue(dict2 instanceof Promise);
    eight.fulfil(8, null);
    assertTrue(((Promise) dict2).isFulfilled());
    assertEquals(Dict.EMPTY.add(1, 2).add(3, 4).add(5, 6).add(7, 8), ((Promise) dict2).unwrap());
  }

  @Test
  public void testUnwrapPromisesTwo() {
    Dict dict1 = Dict.EMPTY.add(1, 2).add(3, new Promise(4)).add(new Promise(5), 6).add(new Promise(7), new Promise(8));
    Object dict2 = dict1.unwrapPromises(null);
    assertTrue(dict2 instanceof Dict);
    assertEquals(Dict.EMPTY.add(1, 2).add(3, 4).add(5, 6).add(7, 8), dict2);
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

  @Test
  public void testDictCollector() {
    Map<?, ?> input = Map.of("a", 1L, "b", 2L, "c", 3L);
    Dict expected = Dict.empty().add("a", 1L).add("b", 2L).add("c", 3L);
    Dict result = input.entrySet().stream().collect(Dict.collect());

    assertEquals(expected.size(), result.size());
    assertEquals(expected, result);
  }
}
