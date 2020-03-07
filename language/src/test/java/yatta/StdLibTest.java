package yatta;

import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class StdLibTest extends CommonTest {
  @Test
  public void sequenceLenTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::len [1, 2, 3]").asLong();
  }

  @Test
  public void sequenceFoldLeftTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::foldl [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceFoldRightTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::foldr [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceFoldLeftWithinLetTest() {
    long ret = context.eval(YattaLanguage.ID, "let xx = 5 in Seq::foldl [1, 2, 3] (\\acc val -> acc + val + xx) 0").asLong();
    assertEquals(21L, ret);
  }

  @Test
  public void sequenceReduceLeftFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::filter \\val -> val < 0 (\\-> 0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceRightFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducer [-2,-1,0,1,2] <| Transducers::filter \\val -> val < 0 (\\-> 0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceLeftDropNTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::drop 2 (\\-> 0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceReduceLeftTakeNTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::take 2 (\\-> 0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceLeftDedupeTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [1, 1, 2, 3, 3, 4] <| Transducers::dedupe (\\-> 0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(20L, ret);
  }

  @Test
  public void sequenceReduceLeftDistinctTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [1, 2, 3, 4, 1, 2, 3, 4] <| Transducers::distinct (\\-> 0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(20L, ret);
  }

  @Test
  public void sequenceReduceLeftChunkTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [6, 1, 5, 2, 4, 3] <| Transducers::chunk 2 (\\-> 1, \\acc val -> acc * Seq::len val, identity)").asLong();
    assertEquals(8L, ret);
  }

  @Test
  public void sequenceReduceLeftScanTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [1, 2, 3] <| Transducers::scan (\\-> 0, \\ acc val -> acc + Seq::len val, identity)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceReduceLeftCatTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [{1, 2}, {3, 4}] <| Transducers::cat (Set::reduce) (\\-> 0, \\acc val -> acc + val, identity)").asLong();
    assertEquals(10L, ret);
  }

  @Test
  public void dictFoldTest() {
    long ret = context.eval(YattaLanguage.ID, "Dict::fold {'a' = 1, 'b' = 2, 'c' = 3} (\\acc _ -> acc + 1) 0").asLong();
    assertEquals(3L, ret);
  }

  @Test
  public void dictReduceMapTest() {
    long ret = context.eval(YattaLanguage.ID, "Dict::reduce {'a' = 1, 'b' = 2, 'c' = 3} <| Transducers::map \\val -> val (\\-> 0, \\ state val -> state + 1, \\state -> state * 2)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void setFoldTest() {
    long ret = context.eval(YattaLanguage.ID, "Set::fold {1, 2, 3} (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void setReduceFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Set::reduce {-2,-1,0,1,2} <| Transducers::filter \\val -> val < 0 (\\-> 0, \\state val -> state + val, \\state -> state * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void setReduceMapTest() {
    long ret = context.eval(YattaLanguage.ID, "Set::reduce {1, 2, 3} <| Transducers::map \\val -> val + 1 (\\-> 0, \\ state val -> state + val, \\state -> state * 2)").asLong();
    assertEquals(18L, ret);
  }

  @Test
  public void systemCommandTest() {
    Value tuple = context.eval(YattaLanguage.ID, "system [\"echo\", \"ahoj\"]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("ahoj", ((List) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  public void simpleEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "eval \"1\"").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void asyncEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "eval \"async \\\\-> 1\"").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void raiseEvalTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "eval \"raise :test \\\"test msg\\\"\"");
      } catch (PolyglotException ex) {
        assertEquals("YattaError <test>: test msg", ex.getMessage());
        throw ex;
      }
    });
  }
}
