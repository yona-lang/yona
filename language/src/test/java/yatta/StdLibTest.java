package yatta;

import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.Test;

import java.util.AbstractList;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class StdLibTest extends CommonTest {
  @Test
  public void sequenceFoldLeftTest() {
    long ret = context.eval(YattaLanguage.ID, "Sequence.foldl [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void sequenceFoldRightTest() {
    long ret = context.eval(YattaLanguage.ID, "Sequence.foldr [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void sequenceFoldLeftWithinLetTest() {
    long ret = context.eval(YattaLanguage.ID, "let xx = 5 in Sequence.foldl [1, 2, 3] (\\acc val -> acc + val + xx) 0").asLong();
    assertEquals(21l, ret);
  }

  @Test
  public void sequenceReduceLeftFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Sequence.reducel [-2,-1,0,1,2] <| Transducers.filter \\val -> val < 0 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6l, ret);
  }

  @Test
  public void sequenceReduceRightFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Sequence.reducer [-2,-1,0,1,2] <| Transducers.filter \\val -> val < 0 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6l, ret);
  }

  @Test
  public void setFoldTest() {
    long ret = context.eval(YattaLanguage.ID, "Set.fold {1, 2, 3} (\\acc val -> acc + val) 0").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void setReduceFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Set.reduce {-2,-1,0,1,2} <| Transducers.filter \\val -> val < 0 (0, \\state val -> state + val, \\state -> state * 2)").asLong();
    assertEquals(-6l, ret);
  }

  @Test
  public void systemCommandTest() {
    Value tuple = context.eval(YattaLanguage.ID, "system [\"echo\", \"ahoj\"]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0l, array[0]);
    assertEquals("ahoj", ((AbstractList) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }
}
