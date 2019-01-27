package abzu;

import abzu.runtime.Tuple;
import org.graalvm.polyglot.Context;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class PatternExpressionTest {

  private Context context;

  @Before
  public void initEngine() {
    context = Context.create();
  }

  @After
  public void dispose() {
    context.close();
  }

  @Test
  public void simpleTuplePatternTest() {
    long ret = context.eval("abzu", "fun arg = case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, 2) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(2l, 3l)).asLong();
    assertEquals(5l, ret);
  }

  @Test
  public void underscorePatternTest() {
    long ret = context.eval("abzu", "fun arg = case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, 2) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(2l, 3l, 4l)).asLong();
    assertEquals(9l, ret);
  }

  @Test
  public void nestedTuplePatternTest() {
    long ret = context.eval("abzu", "fun arg = case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "((1, 2), 3) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(new Tuple(1l, 2l), 3l)).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void nestedUnderscorePatternTest() {
    long ret = context.eval("abzu", "fun arg = case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, _) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 5l)).asLong();
    assertEquals(3l, ret);
  }


  @Test
  public void boundVarPatternTest() {
    long ret = context.eval("abzu", "fun arg bound = case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, bound) -> 1 + bound\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 5l), 5l).asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void freeVarPatternTest() {
    long ret = context.eval("abzu", "fun arg = case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, secondArg) -> 1 + secondArg\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 5l)).asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void freeNestedVarsPatternTest() {
    long ret = context.eval("abzu", "fun arg = case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, secondArg, (nestedThird, 5)) -> nestedThird + secondArg\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 7l, new Tuple(9l, 5l))).asLong();
    assertEquals(16l, ret);
  }
}
