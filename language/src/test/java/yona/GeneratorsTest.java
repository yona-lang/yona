package yona;

import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.TestInstance;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import yona.ast.generators.GeneratedCollection;

import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static yona.ast.generators.GeneratedCollection.*;

@TestInstance(TestInstance.Lifecycle.PER_CLASS)
public class GeneratorsTest extends CommonTest {
  @ParameterizedTest
  @MethodSource("seqTestCases")
  void testSeqGenerator(SeqGeneratorTestCase args) {
    Value ret = context.eval(YonaLanguage.ID, args.toString());
    assertEquals(args.expectedLength, ret.getArraySize());

    Object[] array = ret.as(Object[].class);
    for (int i = 0; i < args.expectedLength; i++) {
      assertEquals(args.expectedValues[i], array[i]);
    }
  }

  static Stream<SeqGeneratorTestCase> seqTestCases() {
    return Stream.of(
        // SEQ -> SEQ
        new SeqGeneratorTestCase("x * 2", "x <- [1, 2, 3]", 3, 2L, 4L, 6L),
        new SeqGeneratorTestCase("x * 2", "x <- async \\-> [1, 2, 3]", 3, 2L, 4L, 6L),
        new SeqGeneratorTestCase("async \\-> x * 2", "x <- [1, 2, 3]", 3, 2L, 4L, 6L),
        new SeqGeneratorTestCase("x * 2", "x <- [1, 2, 3]", "x < 3", 2, 2L, 4L),
        new SeqGeneratorTestCase("x * 2", "x <- [1, 2, 3]", "async \\-> x < 3", 2, 2L, 4L),

        // SET -> SEQ
        new SeqGeneratorTestCase("x * 2", "x <- {1, 2, 3}", 3, 2L, 4L, 6L),
        new SeqGeneratorTestCase("x * 2", "x <- async \\-> {1, 2, 3}", 3, 2L, 4L, 6L),
        new SeqGeneratorTestCase("async \\-> x * 2", "x <- {1, 2, 3}", 3, 2L, 4L, 6L),
        new SeqGeneratorTestCase("x * 2", "x <- {1, 2, 3}", "x < 3", 2, 2L, 4L),
        new SeqGeneratorTestCase("x * 2", "x <- {1, 2, 3}", "async \\-> x < 3", 2, 2L, 4L),

        // DICT -> SEQ
        new SeqGeneratorTestCase("k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", 3, 2L, 6L, 12L),
        new SeqGeneratorTestCase("k * k", "k = _ <- {1 = 2, 2 = 3, 3 = 4}", 3, 1L, 4L, 9L),
        new SeqGeneratorTestCase("v * v", "_ = v <- {1 = 2, 2 = 3, 3 = 4}", 3, 4L, 9L, 16L),
        new SeqGeneratorTestCase("k * v", "k = v <- async \\-> {1 = 2, 2 = 3, 3 = 4}", 3, 2L, 6L, 12L),
        new SeqGeneratorTestCase("async \\-> k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", 3, 2L, 6L, 12L),
        new SeqGeneratorTestCase("k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "v < 4", 2, 2L, 6L),
        new SeqGeneratorTestCase("k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "async \\-> v < 4", 2, 2L, 6L)
        );
  }

  @ParameterizedTest
  @MethodSource("setTestCases")
  void testSetGenerator(SetDictGeneratorTestCase args) {
    boolean ret = context.eval(YonaLanguage.ID, args.expectedValue + " == " + args).asBoolean();
    assertTrue(ret);
  }

  static Stream<SetDictGeneratorTestCase> setTestCases() {
    return Stream.of(
        // SEQ -> SET
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- [1, 2, 3]", "{2, 6, 4}"),
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- async \\-> [1, 2, 3]", "{2, 6, 4}"),
        new SetDictGeneratorTestCase(SET, "async \\-> x * 2", "x <- [1, 2, 3]", "{2, 6, 4}"),
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- [1, 2, 3]", "x < 3", "{2, 4}"),
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- [1, 2, 3]", "async \\-> x < 3", "{2, 4}"),

        // SET -> SET
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- {1, 2, 3}", "{2, 6, 4}"),
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- async \\-> {1, 2, 3}", "{2, 6, 4}"),
        new SetDictGeneratorTestCase(SET, "async \\-> x * 2", "x <- {1, 2, 3}", "{2, 6, 4}"),
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- {1, 2, 3}", "x < 3", "{2, 4}"),
        new SetDictGeneratorTestCase(SET, "x * 2", "x <- {1, 2, 3}", "async \\-> x < 3", "{2, 4}"),

        // DICT -> SET
        new SetDictGeneratorTestCase(SET, "k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "{12, 2, 6}"),
        new SetDictGeneratorTestCase(SET, "k * k", "k = _ <- {1 = 2, 2 = 3, 3 = 4}", "{1, 4, 9}"),
        new SetDictGeneratorTestCase(SET, "v * v", "_ = v <- {1 = 2, 2 = 3, 3 = 4}", "{9, 16, 4}"),
        new SetDictGeneratorTestCase(SET, "k * v", "k = v <- async \\-> {1 = 2, 2 = 3, 3 = 4}", "{12, 2, 6}"),
        new SetDictGeneratorTestCase(SET, "async \\-> k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "{12, 2, 6}"),
        new SetDictGeneratorTestCase(SET, "k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "v < 4", "{2, 6}"),
        new SetDictGeneratorTestCase(SET, "k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "async \\-> v < 4", "{2, 6}")
    );
  }

  @ParameterizedTest
  @MethodSource("dictTestCases")
  void testDictGenerator(SetDictGeneratorTestCase args) {
    boolean ret = context.eval(YonaLanguage.ID, args.expectedValue + " == " + args).asBoolean();
    assertTrue(ret);
  }

  static Stream<SetDictGeneratorTestCase> dictTestCases() {
    return Stream.of(
        // SEQ -> DICT
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- [1, 2, 3]", "{1 = 2, 2 = 4, 3 = 6}"),
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- async \\-> [1, 2, 3]", "{1 = 2, 2 = 4, 3 = 6}"),
        new SetDictGeneratorTestCase(DICT, "x = async \\-> x * 2", "x <- [1, 2, 3]", "{1 = 2, 2 = 4, 3 = 6}"),
        new SetDictGeneratorTestCase(DICT, "async \\-> x = async \\-> x * 2", "x <- [1, 2, 3]", "{1 = 2, 2 = 4, 3 = 6}"),
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- [1, 2, 3]", "x < 3", "{1 = 2, 2 = 4}"),
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- [1, 2, 3]", "async \\-> x < 3", "{1 = 2, 2 = 4}"),

        // SET -> DICT
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- {1, 2, 3}", "{1 = 2, 2 = 4, 3 = 6}"),
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- async \\-> {1, 2, 3}", "{1 = 2, 2 = 4, 3 = 6}"),
        new SetDictGeneratorTestCase(DICT, "x = async \\-> x * 2", "x <- {1, 2, 3}", "{1 = 2, 2 = 4, 3 = 6}"),
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- {1, 2, 3}", "x < 3", "{1 = 2, 2 = 4}"),
        new SetDictGeneratorTestCase(DICT, "x = x * 2", "x <- {1, 2, 3}", "async \\-> x < 3", "{1 = 2, 2 = 4}"),

        // DICT -> DICT
        new SetDictGeneratorTestCase(DICT, "k = k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "{1 = 2, 2 = 6, 3 = 12}"),
        new SetDictGeneratorTestCase(SET, "k = k * k", "k = _ <- {1 = 2, 2 = 3, 3 = 4}", "{1 = 1, 2 = 4, 3 = 9}"),
        new SetDictGeneratorTestCase(SET, "v = v * v", "_ = v <- {1 = 2, 2 = 3, 3 = 4}", "{3 = 9, 4 = 16, 2 = 4}"),
        new SetDictGeneratorTestCase(DICT, "k = k * v", "k = v <- async \\-> {1 = 2, 2 = 3, 3 = 4}", "{1 = 2, 2 = 6, 3 = 12}"),
        new SetDictGeneratorTestCase(DICT, "k = async \\-> k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "{1 = 2, 2 = 6, 3 = 12}"),
        new SetDictGeneratorTestCase(DICT, "k = k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "v < 4", "{1 = 2, 2 = 6}"),
        new SetDictGeneratorTestCase(DICT, "k = k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "async \\-> v < 4", "{1 = 2, 2 = 6}")
    );
  }

  private static class SeqGeneratorTestCase {
    final String reducer, innerCollection, condition;
    final int expectedLength;
    final Object[] expectedValues;

    public SeqGeneratorTestCase(String reducer, String innerCollection, int expectedLength, Object... expectedValues) {
      this.reducer = reducer;
      this.innerCollection = innerCollection;
      this.expectedLength = expectedLength;
      this.expectedValues = expectedValues;
      this.condition = null;
    }

    public SeqGeneratorTestCase(String reducer, String innerCollection, String condition, int expectedLength, Object... expectedValues) {
      this.reducer = reducer;
      this.innerCollection = innerCollection;
      this.condition = condition;
      this.expectedLength = expectedLength;
      this.expectedValues = expectedValues;
    }

    @Override
    public String toString() {
      String body = reducer + " | " + innerCollection + (condition == null ? "" : " if " + condition);
      return "[" + body + "]";
    }
  }

  private static class SetDictGeneratorTestCase {
    final String reducer, innerCollection, condition;
    final String expectedValue;
    final GeneratedCollection type;

    public SetDictGeneratorTestCase(GeneratedCollection type, String reducer, String innerCollection, String expectedValue) {
      this.type = type;
      this.reducer = reducer;
      this.innerCollection = innerCollection;
      this.expectedValue = expectedValue;
      this.condition = null;
    }

    public SetDictGeneratorTestCase(GeneratedCollection type, String reducer, String innerCollection, String condition, String expectedValue) {
      this.type = type;
      this.reducer = reducer;
      this.innerCollection = innerCollection;
      this.condition = condition;
      this.expectedValue = expectedValue;
    }

    @Override
    public String toString() {
      String body = reducer + " | " + innerCollection + (condition == null ? "" : " if " + condition);
      return switch (type) {
        case SEQ -> "[" + body + "]";
        case SET, DICT -> "{" + body + "}";
      };
    }
  }
}
