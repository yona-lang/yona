package yona;

import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestInstance;

import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.*;

@TestInstance(TestInstance.Lifecycle.PER_CLASS)
public class SimpleExpressionTest extends CommonTest {
  @Test
  public void longValueTest() {
    long ret = context.eval(YonaLanguage.ID, "5").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void charValueTest() {
    int ret = context.eval(YonaLanguage.ID, "'a'").asInt();
    assertEquals('a', ret);
  }

  @Test
  public void byteValueTest() {
    byte ret = context.eval(YonaLanguage.ID, "5b").asByte();
    assertEquals(5, ret);
  }

  @Test
  public void floatValueTest() {
    double ret = context.eval(YonaLanguage.ID, "5.0").asDouble();
    assertEquals(5.0, ret, 0);
  }

  @Test
  public void floatSuffixValueTest() {
    double ret = context.eval(YonaLanguage.ID, "5f").asDouble();
    assertEquals(5.0, ret, 0);
  }

  @Test
  public void unitValueTest() {
    assertTrue(context.eval(YonaLanguage.ID, "()").isNull());
  }

  @Test
  public void stringValueTest() {
    String ret = context.eval(YonaLanguage.ID, "\"yona-string\"").asString();
    assertEquals("yona-string", ret);
  }

  @Test
  public void symbolValueTest() {
    String ret = context.eval(YonaLanguage.ID, ":yonaSymbol").asString();
    assertEquals("yonaSymbol", ret);
  }

  @Test
  public void tupleValueTest() {
    Value tuple = context.eval(YonaLanguage.ID, "(1, 2, 3)");
    assertEquals(3, tuple.getArraySize());

    Object[] array = tuple.as(Object[].class);
    assertEquals(1L, array[0]);
    assertEquals(2L, array[1]);
    assertEquals(3L, array[2]);
  }

  @Test
  public void minusTest() {
    long ret = context.eval(YonaLanguage.ID, "5- 2").asLong();
    assertEquals(3L, ret);
  }

  @Test
  public void setValueTest() {
    String ret = context.eval(YonaLanguage.ID, "{1, 2, 3}").asString();
    assertEquals("{1, 2, 3}", ret);
  }

  @Test
  public void emptySequenceValueTest() {
    Value sequence = context.eval(YonaLanguage.ID, "[]");
    assertEquals(0, sequence.getArraySize());
  }

  @Test
  public void oneSequenceValueTest() {
    Value sequence = context.eval(YonaLanguage.ID, "[1]");
    assertEquals(1, sequence.getArraySize());

    Object[] array = sequence.as(Object[].class);
    assertEquals(1L, array[0]);
  }

  @Test
  public void twoSequenceValueTest() {
    Value sequence = context.eval(YonaLanguage.ID, "[1, 2]");
    assertEquals(2, sequence.getArraySize());

    Object[] array = sequence.as(Object[].class);
    assertEquals(1L, array[0]);
    assertEquals(2L, array[1]);
  }

  @Test
  public void sequenceToStrValueTest() {
    String ret = context.eval(YonaLanguage.ID, "[1, 2] |> str").asString();
    assertEquals("[1, 2]", ret);
  }

  @Test
  public void threeSequenceValueTest() {
    Value sequence = context.eval(YonaLanguage.ID, "[1, 2, 3]");
    assertEquals(3, sequence.getArraySize());

    Object[] array = sequence.as(Object[].class);
    assertEquals(1L, array[0]);
    assertEquals(2L, array[1]);
    assertEquals(3L, array[2]);
  }

  @Test
  public void zeroArgFunctionTest() {
    long ret = context.eval(YonaLanguage.ID, "\\ -> 5").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void oneArgFunctionTest() {
    long ret = context.eval(YonaLanguage.ID, "\\arg -> arg").execute(6).asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void twoArgFunctionFirstTest() {
    long ret = context.eval(YonaLanguage.ID, "\\argone argtwo -> argone").execute(5, 6).asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void twoArgFunctionSecondTest() {
    long ret = context.eval(YonaLanguage.ID, "\\argone argtwo -> argtwo").execute(5, 6).asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void moduleTest() {
    String src = "module TestMod exports fun as\n" +
        "fun = 6\n" +
        "other_fun = 7\n" +
        "end";
    Value modVal = context.eval(YonaLanguage.ID, src);

    assertTrue(modVal.hasMember("fun"));
    assertTrue(modVal.hasMember("other_fun"));
    assertFalse(modVal.hasMember("whatever"));

    assertEquals(6L, modVal.getMember("fun").execute().asLong());
    assertEquals(7L, modVal.getMember("other_fun").execute().asLong());
  }

  @Test
  public void letOneAliasTest() {
    long ret = context.eval(YonaLanguage.ID, "\\test -> let alias = test in alias").execute(5).asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void letTwoAliasesTest() {
    Value ret = context.eval(YonaLanguage.ID, "\\test -> let\n" +
        "    alias = test\n" +
        "    aliastwo = 6\n" +
        "in\n" +
        "(alias, aliastwo)").execute(5L);
    assertEquals(2, ret.getArraySize());

    Object[] array = ret.as(Object[].class);
    assertEquals(5L, array[0]);
    assertEquals(6L, array[1]);
  }

  @Test
  public void letNotInFunctionTest() {
    long ret = context.eval(YonaLanguage.ID, "let alias = 6 in alias").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void letFunctionAliasTest() {
    long ret = context.eval(YonaLanguage.ID, "let funalias = \\arg -> arg in funalias").execute(5).asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void lambdaInLetTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "alias = 6\n" +
        "funalias = \\arg -> alias\n" +
        "in funalias").execute(5L).asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void invocationInLetTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "funone = \\arg -> arg\n" +
        "alias = 6\n" +
        "funalias = \\arg -> funone alias\n" +
        "in funalias 5").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void curriedLambdaInLetTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "curriedFun = \\argone argtwo -> argone\n" +
        "curriedOne = \\curriedOneArg -> curriedFun curriedOneArg 6\n" +
        "curried = \\curriedArg -> curriedOne curriedArg\n" +
        "in curried 5").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void curriedLambdaInLetSecondArgTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "curriedFun = \\argone argtwo -> argtwo\n" +
        "curriedOne = \\curriedOneArg -> curriedFun curriedOneArg 6\n" +
        "curried = \\curriedArg -> curriedOne curriedArg\n" +
        "in curried 6").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void curriedLambdaInLetOutOfScopeTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "curriedFun = let fullFun = \\argone argtwo argthree -> argthree in fullFun 1\n" +
        "curried = \\curriedArg -> curriedFun 3 curriedArg\n" +
        "in curried 6").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void curriedLambdaInLetZeroArgsTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "curriedFun = \\ -> 1\n" +
        "curried = \\curriedArg -> curriedFun\n" +
        "in curried").execute(6).asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void zeroArgApplicationInLetTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "zeroArgFun = \\-> 5\n" +
        "in zeroArgFun").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void moduleCallPrivateInLetTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "testMod = module TestMod exports funone as\n" +
        "funone argone = funtwo argone\n" +
        "funtwo argone = argone\n" +
        "end in testMod::funone 6").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void intSumTest() {
    long ret = context.eval(YonaLanguage.ID, "2 + 5").asLong();
    assertEquals(7L, ret);
  }

  @Test
  public void simpleDoTest() {
    long ret = context.eval(YonaLanguage.ID, "do\n" +
        "one = 1\n" +
        "IO::println one\n" +
        "two = 2\n" +
        "one + two\n" +
        "end\n").asLong();
    assertEquals(3L, ret);
  }

  @Test
  public void simpleRaiseTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YonaLanguage.ID, "raise :random_error \"something happened\"\n");
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "YonaError <random_error>: something happened");
        throw ex;
      }
    });
  }

  @Test
  public void promiseRaiseTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YonaLanguage.ID, "raise :random_error \"something {(async \\-> \"happened\")}\"\n");
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "YonaError <random_error>: something happened");
        throw ex;
      }
    });
  }

  @Test
  public void simpleTryCatchTest() {
    String ret = context.eval(YonaLanguage.ID, "try\n" +
        "raise :random_error \"something happened\"\n" +
        "catch\n" +
        "(:not_this, _, _) -> \"nothing\"\n" +
        "(:random_error, message, stacktrace) -> message\n" +
        "end\n").asString();

    assertEquals("YonaError <random_error>: something happened", ret);
  }

  @Test
  public void simpleTryCatchNoErrorTest() {
    String ret = context.eval(YonaLanguage.ID, "try\n" +
        "\"no error\"\n" +
        "catch\n" +
        "(:not_this, _, _) -> \"nothing\"\n" +
        "(:random_error, message, stacktrace) -> message\n" +
        "end\n").asString();

    assertEquals("no error", ret);
  }

  @Test
  public void noMatchExceptionTest() {
    String ret = context.eval(YonaLanguage.ID, "try case 1 of\n" +
        "2 -> 0\n" +
        "end\n" +
        "catch\n" +
        "(:not_this, _, _) -> \"nothing\"\n" +
        "(:nomatch, message, stacktrace) -> message\n" +
        "end\n").asString();

    assertEquals("NoMatchException: 1", ret);
  }

  @Test
  public void matchAnyExceptionTest() {
    long ret = context.eval(YonaLanguage.ID, "try case 1 of\n" +
        "2 -> 0\n" +
        "end\n" +
        "catch\n" +
        "(:not_this, _, _) -> 1\n" +
        "_ -> 2\n" +
        "end\n").asLong();

    assertEquals(2L, ret);
  }

  @Test
  public void simpleBackTickTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    func = \\aa bb -> aa + bb\n" +
        "in 2 `func` 3").asLong();

    assertEquals(5L, ret);
  }

  @Test
  public void leftExpressionBackTickTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    func = \\aa bb -> aa + bb\n" +
        "in (let xx = 2 in xx) `func` 3").asLong();

    assertEquals(5L, ret);
  }

  @Test
  public void curryingBackTickTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    func = \\aa bb cc -> aa + bb + cc\n" +
        "    curried = 2 `func` 3\n" +
        "in curried 4").asLong();

    assertEquals(9L, ret);
  }

  @Test
  public void logicalNotTrueTest() {
    boolean ret = context.eval(YonaLanguage.ID, "!true").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void logicalNotTruePromiseTest() {
    boolean ret = context.eval(YonaLanguage.ID, "!(async \\->true)").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void logicalNotFalseTest() {
    boolean ret = context.eval(YonaLanguage.ID, "!false").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void binaryNotTest() {
    long ret = context.eval(YonaLanguage.ID, "~0").asLong();
    assertEquals(-1L, ret);
  }

  @Test
  public void binaryNotPromiseTest() {
    long ret = context.eval(YonaLanguage.ID, "~(async \\->0)").asLong();
    assertEquals(-1L, ret);
  }

  @Test
  public void singleLetterNames() {
    long ret = context.eval(YonaLanguage.ID, "\\a b -> a+b").execute(1L, 2L).asLong();
    assertEquals(3L, ret);
  }

  @Test
  public void joinAssociativity() {
    Value ret = context.eval(YonaLanguage.ID, "[1, 2] ++ [3, 4] ++ [5, 6]");
    assertEquals(6, ret.getArraySize());
  }

  @Test
  public void consRightAssociativity() {
    Value ret = context.eval(YonaLanguage.ID, "[1, 2] |- 3 |- 4");
    assertEquals(4, ret.getArraySize());
    Long[] array = ret.as(Long[].class);
    assertEquals(1L, array[0]);
    assertEquals(2L, array[1]);
    assertEquals(3L, array[2]);
    assertEquals(4L, array[3]);
  }

  @Test
  public void consLeftAssociativity() {
    Value ret = context.eval(YonaLanguage.ID, "1 -| 2 -| [3, 4]");
    assertEquals(4, ret.getArraySize());
    Long[] array = ret.as(Long[].class);
    assertEquals(1L, array[0]);
    assertEquals(2L, array[1]);
    assertEquals(3L, array[2]);
    assertEquals(4L, array[3]);
  }

  @Test
  public void simplePipeRight() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    plus = \\a b -> a + b\n" +
        "    multiply = \\a b -> a * b\n" +
        "in 5 |> plus 3 |> multiply 10").asLong();
    assertEquals(80L, ret);
  }

  @Test
  public void newLinesPipeRight() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    plus = \\a b -> a + b\n" +
        "    multiply = \\a b -> a * b\n" +
        "in 5 \n" +
        "|> plus 3\n" +
        "|> multiply 10").asLong();
    assertEquals(80L, ret);
  }

  @Test
  public void simplePipeLeft() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    plus = \\a b -> a + b\n" +
        "    multiply = \\a b -> a * b\n" +
        "in plus 5 <| multiply 10 <| 3").asLong();
    assertEquals(35L, ret);
  }

  @Test
  public void newLinesPipeLeft() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    plus = \\a b -> a + b\n" +
        "    multiply = \\a b -> a * b\n" +
        "in plus 5" +
        "<| multiply 10" +
        "<| 3").asLong();
    assertEquals(35L, ret);
  }

  @Test
  public void simpleOperatorPrecedenceAndApplication() {
    long ret = context.eval(YonaLanguage.ID, "identity 5 + 3").asLong();
    assertEquals(8L, ret);
  }

  @Test
  public void simplePrintlnTest() {
    context.eval(YonaLanguage.ID, "IO::println \"hello\"");
  }

  @Test
  public void dynamicModuleCallTest() {
    String src = "let testMod = module Test exports fun as\n" +
        "fun = 6\n" +
        "other_fun = 7\n" +
        "end\n" +
        "in testMod::fun";
    long ret = context.eval(YonaLanguage.ID, src).asLong();

    assertEquals(6L, ret);
  }

  @Test
  public void dynamicAsyncModuleCallTest() {
    String src = "let testMod = async \\-> module Test exports fun as\n" +
        "fun = 6\n" +
        "other_fun = 7\n" +
        "end\n" +
        "in testMod::fun";
    long ret = context.eval(YonaLanguage.ID, src).asLong();

    assertEquals(6L, ret);
  }

  @Test
  public void simplePartiallyInitializedRecordTest() {
    Value tuple = context.eval(YonaLanguage.ID, "module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = TestRecord(argone = 1)\n" +
        "end").getMember("funone").execute();

    assertEquals(3, tuple.getArraySize());

    Object[] array = tuple.as(Object[].class);
    assertEquals("TestRecord", array[0]);
    assertEquals(1L, array[1]);
    assertNull(array[2]);
  }

  @Test
  public void nestedModuleRecordTest() {
    Value tuple = context.eval(YonaLanguage.ID, "module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let nestedModule = module NestedModule exports funtwo as\n" +
        "funtwo = TestRecord(argtwo = 1)\n" +
        "end\n" +
        "in nestedModule::funtwo\n" +
        "end").getMember("funone").execute();

    assertEquals(3, tuple.getArraySize());

    Object[] array = tuple.as(Object[].class);
    assertEquals("TestRecord", array[0]);
    assertNull(array[1]);
    assertEquals(1L, array[2]);
  }

  @Test
  public void simpleRecordFieldAccessTest() {
    long ret = context.eval(YonaLanguage.ID, "module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let rec = TestRecord(argone = 1) in\n" +
        "rec.argone\n" +
        "end").getMember("funone").execute().asLong();

    assertEquals(1L, ret);
  }

  @Test
  public void recordFromFunctionFieldAccessTest() {
    long ret = context.eval(YonaLanguage.ID, "module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let rec = \\-> TestRecord(argone = 1) in\n" +
        "rec.argone\n" +
        "end").getMember("funone").execute().asLong();

    assertEquals(1L, ret);
  }

  @Test
  public void emptyRecordFieldAccessTest() {
    Value ret = context.eval(YonaLanguage.ID, "module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let rec = TestRecord(argone = 1) in\n" +
        "rec.argtwo\n" +
        "end").getMember("funone").execute();

    assertTrue(ret.isNull());
  }

  @Test
  public void promiseRecordFieldAccessTest() {
    long ret = context.eval(YonaLanguage.ID, "let mod = module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let rec = async \\-> TestRecord(argone = async \\-> 1) in\n" +
        "rec.argone\n" +
        "end in mod::funone").asLong();

    assertEquals(1L, ret);
  }

  @Test
  public void simpleRecordUpdateOneTest() {
    long ret = context.eval(YonaLanguage.ID, "module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let rec = TestRecord(argone = 1) in\n" +
        "let rectwo = rec(argone = 2) in rectwo.argone\n" +
        "end").getMember("funone").execute().asLong();

    assertEquals(2L, ret);
  }

  @Test
  public void simpleRecordUpdateTwoTest() {
    long ret = context.eval(YonaLanguage.ID, "module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let rec = TestRecord(argone = 1) in\n" +
        "let rectwo = rec(argtwo = 2) in rectwo.argtwo\n" +
        "end").getMember("funone").execute().asLong();

    assertEquals(2L, ret);
  }

  @Test
  public void promiseRecordUpdateTest() {
    long ret = context.eval(YonaLanguage.ID, "let mod = module RecordModule exports funone as\n" +
        "record TestRecord = (argone, argtwo)\n" +
        "funone = let rec = async \\-> TestRecord(argone = 1) in\n" +
        "let rectwo = rec(argtwo = 2) in rectwo.argtwo\n" +
        "end in\n" +
        "mod::funone").asLong();

    assertEquals(2L, ret);
  }

  @Test
  public void closureTest() {
    long ret = context.eval(YonaLanguage.ID, "\\a b -> let\n" +
        "x = \\-> a + b\n" +
        "in x").execute(1L, 2L).asLong();

    assertEquals(3L, ret);
  }

  @Test
  public void calculatePiTest() {
    double ret = context.eval(YonaLanguage.ID, "let\n" +
        "    calculator = module PiCalculator exports run as\n" +
        "        iteration i = (-1f ** (i + 1f)) / ((2f * i) - 1f)\n" +
        "        max_iterations = 10000\n" +
        "        run i acc\n" +
        "          | i >= max_iterations = acc\n" +
        "          | true = run (i + 1) (acc + (iteration <| float i))\n" +
        "    end\n" +
        "in\n" +
        "    4f * calculator::run 1 0f").asDouble();

    assertTrue(3d < ret);
    assertTrue(4d > ret);
  }

  @Test
  public void contextManagerTest() {
    long ret = context.eval(YonaLanguage.ID, "with context\\Local::new \"test_context\" (\\ctx_mgr cb -> (cb) * 2) 4 as test_context let data = context\\Local::get_data test_context in data * 2 end").asLong();
    assertEquals(16L, ret);
  }

  @Test
  public void asyncContextManagerTest() {
    long ret = context.eval(YonaLanguage.ID, "with context\\Local::new (async \\-> \"test_context\") (\\ctx_mgr cb -> async \\-> (cb) * 2) (async \\-> 4) as test_context let data = context\\Local::get_data test_context in data * 2 end").asLong();
    assertEquals(16L, ret);
  }

  @Test
  public void emptyDoTest() {
    Value ret = context.eval(YonaLanguage.ID, "do\nend");
    assertTrue(ret.isNull());
  }

  @Test
  public void simpleRangeTest() {
    Value sequence = context.eval(YonaLanguage.ID, "[0..3]");
    assertEquals(3, sequence.getArraySize());

    Object[] array = sequence.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals(1L, array[1]);
    assertEquals(2L, array[2]);
  }

  @Test
  public void simpleAsyncRangeTest() {
    Value sequence = context.eval(YonaLanguage.ID, "[(async \\->0)..3]");
    assertEquals(3, sequence.getArraySize());

    Object[] array = sequence.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals(1L, array[1]);
    assertEquals(2L, array[2]);
  }

  @Test
  public void rangeWithStepTest() {
    Value sequence = context.eval(YonaLanguage.ID, "[2, 5..10]");
    assertEquals(3, sequence.getArraySize());

    Object[] array = sequence.as(Object[].class);
    assertEquals(5L, array[0]);
    assertEquals(7L, array[1]);
    assertEquals(9L, array[2]);
  }

  //docs state:
  //If the {@link #HAS_SIZE} message
  //     * returns <code>true</code> implementations for {@link #READ} and {@link #WRITE} messages with
  //     * {@link Integer} parameters from range <code>0</code> to <code>GET_SIZE - 1</code> are
  //     * required.
  //which dictionary-like structures can't support by their nature
  /*@Test
  public void emptyDictValueTest() {
    Value dict = context.eval(YonaLanguage.ID, "{}");
    assertEquals(0, dict.getArraySize());
  }

  @Test
  public void dictValueTest() {
    Value dict = context.eval(YonaLanguage.ID, "{:aa = 1, :bb = 2}");
    assertEquals(2, dict.getArraySize());
  }*/
}
