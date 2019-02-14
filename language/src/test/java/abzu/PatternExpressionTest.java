package abzu;

import abzu.runtime.Sequence;
import abzu.runtime.Tuple;
import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.Value;
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
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, 2) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(2l, 3l)).asLong();
    assertEquals(5l, ret);
  }

  @Test
  public void underscorePatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, 2) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(2l, 3l, 4l)).asLong();
    assertEquals(9l, ret);
  }

  @Test
  public void nestedTuplePatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "((1, 2), 3) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(new Tuple(1l, 2l), 3l)).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void nestedUnderscorePatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, _) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 5l)).asLong();
    assertEquals(3l, ret);
  }


  @Test
  public void boundVarPatternTest() {
    long ret = context.eval("abzu", "\\arg bound -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, bound) -> 1 + bound\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 5l), 5l).asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void freeVarPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, secondArg) -> 1 + secondArg\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 5l)).asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void freeNestedVarsPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, secondArg, (nestedThird, 5)) -> nestedThird + secondArg\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n").execute(new Tuple(1l, 7l, new Tuple(9l, 5l))).asLong();
    assertEquals(16l, ret);
  }

  @Test
  public void simpleIntPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "1 -> 2\n" +
        "2 -> 3\n"+
        "_ -> 9\n").execute(1l).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void simpleStringPatternTest() {
    String ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "\"foo\" -> \"bar\"\n"+
        "\"hello\" -> \"world\"\n" +
        "_ -> \"unexpected\"\n").execute("hello").asString();
    assertEquals("world", ret);
  }

  @Test
  public void headTailsPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "1:[] -> 2\n" +
        "[] -> 3\n"+
        "_ -> 9\n").execute(Sequence.sequence(1l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void headTailsUnderscoreOnePatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "1:_ -> 2\n" +
        "_:_ -> 3\n" +
        "[] -> 4\n"+
        "_ -> 9\n").execute(Sequence.sequence(1l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void headTailsUnderscoreTwoPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "1:_ -> 2\n" +
        "_:_ -> 3\n" +
        "[] -> 4\n"+
        "_ -> 9\n").execute(Sequence.sequence(2l)).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void headTailsUnderscoreThreePatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "1:_ -> 2\n" +
        "_:_ -> 4\n"+
        "_ -> 9\n").execute(Sequence.sequence()).asLong();
    assertEquals(4l, ret);
  }

  @Test
  public void headTailsUnderscoreFourPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "1:_ -> 2\n" +
        "_:[] -> 4\n"+
        "_ -> 9\n").execute(Sequence.sequence()).asLong();
    assertEquals(4l, ret);
  }

  @Test
  public void headTailsFreeVarPatternTest() {
    Value sequence = context.eval("abzu", "\\arg -> case arg of\n" +
        "1:[] -> 2\n" +
        "1:tail -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(2l, array[0]);
    assertEquals(3l, array[1]);
  }

  @Test
  public void headTailsFreeVarEmptyPatternTest() {
    Value sequence = context.eval("abzu", "\\arg -> case arg of\n" +
        "1:tail -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n").execute(Sequence.sequence(1l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(0, array.length);
  }

  @Test
  public void headTailsBoundVarPatternTest() {
    Value sequence = context.eval("abzu", "\\arg bound -> case arg of\n" +
        "1:[] -> 2\n" +
        "1:bound -> bound\n" +
        "[] -> 3\n"+
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l), Sequence.sequence(2l, 3l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(2l, array[0]);
    assertEquals(3l, array[1]);
  }

  @Test
  public void headTailsBoundVarEmptyPatternTest() {
    Value sequence = context.eval("abzu", "\\arg bound -> case arg of\n" +
        "1:bound -> bound\n" +
        "[] -> 3\n"+
        "_ -> 9\n").execute(Sequence.sequence(1l), Sequence.sequence());

    Object[] array = sequence.as(Object[].class);

    assertEquals(0, array.length);
  }

  @Test
  public void sequenceMatchPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "[1, _, 3] -> 1\n" +
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l)).asLong();

    assertEquals(1l, ret);
  }

  @Test
  public void sequenceMatchFreeVarPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "[1, second, 3] -> second\n" +
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l)).asLong();

    assertEquals(2l, ret);
  }

  @Test
  public void sequenceMatchBoundVarPatternTest() {
    long ret = context.eval("abzu", "\\arg bound -> case arg of\n" +
        "[] -> 3\n"+
        "[1, bound, 3] -> bound\n" +
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l), 2l).asLong();

    assertEquals(2l, ret);
  }


  @Test
  public void namedSequenceMatchPatternTest() {
    Value sequence = context.eval("abzu", "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "seq@([1, _, 3]) -> seq\n" +
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(3, array.length);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
    assertEquals(3l, array[2]);
  }

  @Test
  public void namedSequenceMatchFreeVarPatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "seq@([1, second, 3]) -> second\n" +
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l)).asLong();

    assertEquals(2l, ret);
  }

  @Test
  public void namedSequenceMatchBoundVarPatternTest() {
    long ret = context.eval("abzu", "\\arg bound -> case arg of\n" +
        "[] -> 3\n"+
        "seq@([1, bound, 3]) -> bound\n" +
        "_ -> 9\n").execute(Sequence.sequence(1l, 2l, 3l), 2l).asLong();

    assertEquals(2l, ret);
  }

  @Test
  public void tupleInLetPatternTest() {
    long ret = context.eval("abzu", "let (1, x, _) = (1, 2, 3) in x").asLong();

    assertEquals(2l, ret);
  }


  @Test
  public void sequenceInLetPatternTest() {
    long ret = context.eval("abzu", "let [1, x, y] = [1, 2, 3] in x + y").asLong();

    assertEquals(5l, ret);
  }

  @Test
  public void guardsInCasePatternTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(aa, bb)\n" +
        "| aa <= bb -> aa\n" +
        "| aa > bb -> bb\n").execute(new Tuple(1l, 2l)).asLong();
    assertEquals(1l, ret);
  }

  @Test
  public void guardsInCasePatternSecondTest() {
    long ret = context.eval("abzu", "\\arg -> case arg of\n" +
        "(aa, bb)\n" +
        "| aa <= bb -> aa\n" +
        "| aa > bb -> bb\n").execute(new Tuple(3l, 2l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void guardsInCaseInLambdaTest() {
    String ret = context.eval("abzu", "\\arg -> case () of\n" +
        "_ | arg < 0     -> \"negative\"\n" +
        "  | arg == 0    -> \"zero\"\n" +
        "  | true        -> \"positive\"\n").execute(0l).asString();
    assertEquals("zero", ret);
  }
}
