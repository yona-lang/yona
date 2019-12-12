package yatta;

import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.Test;
import yatta.runtime.Dictionary;
import yatta.runtime.Seq;
import yatta.runtime.Tuple;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class PatternExpressionTest extends CommonTest {
  @Test
  public void simpleTuplePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, 2) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n" +
        "end\n").execute(new Tuple(2l, 3l)).asLong();
    assertEquals(5l, ret);
  }

  @Test
  public void underscorePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, 2) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n" +
        "end\n").execute(new Tuple(2l, 3l, 4l)).asLong();
    assertEquals(9l, ret);
  }

  @Test
  public void nestedTuplePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "((1, 2), 3) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n" +
        "end\n").execute(new Tuple(new Tuple(1l, 2l), 3l)).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void nestedUnderscorePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, _) -> 3\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n" +
        "end\n").execute(new Tuple(1l, 5l)).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void boundVarPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, bound) -> 1 + bound\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n" +
        "end\n").execute(new Tuple(1l, 5l), 5l).asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void freeVarPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, secondArg) -> 1 + secondArg\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n" +
        "end\n").execute(new Tuple(1l, 5l)).asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void freeNestedVarsPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(1, 2, 3) -> 6\n" +
        "(1, secondArg, (nestedThird, 5)) -> nestedThird + secondArg\n"+
        "(2, 3) -> 5\n"+
        "_ -> 9\n" +
        "end\n").execute(new Tuple(1l, 7l, new Tuple(9l, 5l))).asLong();
    assertEquals(16l, ret);
  }

  @Test
  public void simpleIntPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 -> 2\n" +
        "2 -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(1l).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void simpleStringPatternTest() {
    String ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "\"foo\" -> \"bar\"\n"+
        "\"hello\" -> \"world\"\n" +
        "_ -> \"unexpected\"\n" +
        "end\n").execute(Seq.fromCharSequence("hello")).asString();
    assertEquals("world", ret);
  }

  @Test
  public void headTailsPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: [] -> 2\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void tailsHeadPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "[] :> 1 -> 2\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void headTailsUnderscoreOnePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: _ -> 2\n" +
        "_ <: _ -> 3\n" +
        "[] -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void tailsHeadUnderscoreOnePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "_ :> 1 -> 2\n" +
        "_ :> _ -> 3\n" +
        "[] -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void headTailsUnderscoreTwoPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: _ -> 2\n" +
        "_ <: _ -> 3\n" +
        "[] -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(2l)).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void tailsHeadUnderscoreTwoPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "_ :> 1 -> 2\n" +
        "_ :> _ -> 3\n" +
        "[] -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(2l)).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void headTailsUnderscoreThreePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: _ -> 2\n" +
        "_ <: _ -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(2l)).asLong();
    assertEquals(4l, ret);
  }

  @Test
  public void tailsHeadUnderscoreThreePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "_ :> 1 -> 2\n" +
        "_ :> _ -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(2l)).asLong();
    assertEquals(4l, ret);
  }
  @Test
  public void headTailsUnderscoreFourPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: _ -> 2\n" +
        "_ <: [] -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(2l)).asLong();
    assertEquals(4l, ret);
  }

  @Test
  public void tailsHeadUnderscoreFourPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "_ :> 1 -> 2\n" +
        "[] :> _ -> 4\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(2l)).asLong();
    assertEquals(4l, ret);
  }

  @Test
  public void headTailsFreeVarPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: [] -> 2\n" +
        "1 <: tail -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(2l, array[0]);
    assertEquals(3l, array[1]);
  }

  @Test
  public void tailsHeadFreeVarPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "[] :> 3 -> 2\n" +
        "tail :> 3 -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
  }

  @Test
  public void headTailsFreeVarEmptyPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: tail -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(0, array.length);
  }

  @Test
  public void tailsHeadFreeVarEmptyPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "tail :> 1 -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(0, array.length);
  }

  @Test
  public void headTailsBoundVarPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "1 <: [] -> 2\n" +
        "1 <: bound -> bound\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l), Seq.sequence(2l, 3l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(2l, array[0]);
    assertEquals(3l, array[1]);
  }

  @Test
  public void tailsHeadBoundVarPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "[] :> 3 -> 2\n" +
        "bound :> 3 -> bound\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l), Seq.sequence(1l, 2l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
  }

  @Test
  public void headTailsBoundVarEmptyPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "1 <: bound -> bound\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l), Seq.EMPTY);

    Object[] array = sequence.as(Object[].class);

    assertEquals(0, array.length);
  }

  @Test
  public void tailsHeadBoundVarEmptyPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "bound :> 1 -> bound\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l), Seq.EMPTY);

    Object[] array = sequence.as(Object[].class);

    assertEquals(0, array.length);
  }

  @Test
  public void sequenceMatchPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "[1, _, 3] -> 1\n" +
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l)).asLong();

    assertEquals(1l, ret);
  }

  @Test
  public void sequenceMatchFreeVarPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "[1, second, 3] -> second\n" +
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l)).asLong();

    assertEquals(2l, ret);
  }

  @Test
  public void sequenceMatchBoundVarPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "[] -> 3\n"+
        "[1, bound, 3] -> bound\n" +
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l), 2l).asLong();

    assertEquals(2l, ret);
  }


  @Test
  public void namedSequenceMatchPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "seq@([1, _, 3]) -> seq\n" +
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(3, array.length);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
    assertEquals(3l, array[2]);
  }

  @Test
  public void namedSequenceMatchFreeVarPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "[] -> 3\n"+
        "seq@([1, second, 3]) -> second\n" +
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l)).asLong();

    assertEquals(2l, ret);
  }

  @Test
  public void namedSequenceMatchBoundVarPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "[] -> 3\n"+
        "seq@([1, bound, 3]) -> bound\n" +
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l), 2l).asLong();

    assertEquals(2l, ret);
  }

  @Test
  public void tupleInLetPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "let (1, x, _) = (1, 2, 3) in x").asLong();

    assertEquals(2l, ret);
  }


  @Test
  public void sequenceInLetPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "let [1, x, y] = [1, 2, 3] in x + y").asLong();

    assertEquals(5l, ret);
  }

  @Test
  public void guardsInCasePatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(aa, bb)\n" +
        "| aa <= bb -> aa\n" +
        "| aa > bb -> bb\n" +
        "end\n").execute(new Tuple(1l, 2l)).asLong();
    assertEquals(1l, ret);
  }

  @Test
  public void guardsInCasePatternSecondTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "(aa, bb)\n" +
        "| aa <= bb -> aa\n" +
        "| aa > bb -> bb\n" +
        "end\n").execute(new Tuple(3l, 2l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void guardsInCaseInLambdaTest() {
    String ret = context.eval(YattaLanguage.ID, "\\arg -> case () of\n" +
        "_ | arg < 0     -> \"negative\"\n" +
        "  | arg == 0    -> \"zero\"\n" +
        "  | true        -> \"positive\"\n" +
        "end\n").execute(0l).asString();
    assertEquals("zero", ret);
  }

  @Test
  public void multipleHeadsOneTailPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: 2 <: [] -> 2\n" +
        "1 <: 2 <: tail -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l, 4l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(3l, array[0]);
    assertEquals(4l, array[1]);
  }

  @Test
  public void oneTailMultipleHeadsPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "[] :> 3 :> 4  -> 2\n" +
        "tail :> 3 :> 4 -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(1l, 2l, 3l, 4l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
  }

  @Test
  public void dictPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "{\"b\" = 3} -> 3\n" +
        "{\"a\" = 1, \"b\" = bb} -> bb\n" +
        "_ -> 9\n" +
        "end\n").execute(Dictionary.dictionary().insert(Seq.fromCharSequence("a"), 1l).insert(Seq.fromCharSequence("b"), 2l)).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void emptyDictPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "{} -> 3\n" +
        "_ -> 9\n" +
        "end\n").execute(Dictionary.dictionary()).asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void nonEmptyDictPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "{} -> 3\n" +
        "_ -> 9\n" +
        "end\n").execute(Dictionary.dictionary().insert("a", 1l)).asLong();
    assertEquals(9l, ret);
  }

  @Test
  public void dictBoundVarPatternTest() {
    long ret = context.eval(YattaLanguage.ID, "\\arg bound -> case arg of\n" +
        "{\"b\" = 3} -> 3\n" +
        "{\"a\" = 1, \"b\" = bound} -> bound\n" +
        "_ -> 9\n" +
        "end\n").execute(Dictionary.dictionary().insert(Seq.fromCharSequence("a"), 1l).insert(Seq.fromCharSequence("b"), 2l), 2l).asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void headTailsHeadPatternTest() {
    Value sequence = context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
        "1 <: 2 <: 3 <: tail :> 3 :> 4 -> 1 \n" +
        "1 <: 2 <: [] :> 3 :> 4  -> 2\n" +
        "0 <: tail :> 3 :> 4 -> tail\n" +
        "[] -> 3\n"+
        "_ -> 9\n" +
        "end\n").execute(Seq.sequence(0l, 1l, 2l, 3l, 4l));

    Object[] array = sequence.as(Object[].class);

    assertEquals(2, array.length);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
  }
  
  @Test
  public void nestedCaseSyntaxTest() {
    long ret = context.eval(YattaLanguage.ID, "case [1, 2, 3] of\n" +
        "[1, 2] -> 0\n" +
        "[1, middle, 3] -> case middle of\n" +
        "    1 -> 1\n" +
        "    2 -> 2\n" +
        "end\n" +
        "_ -> 3\n" +
        "end").asLong();

    assertEquals(2l, ret);
  }
}
