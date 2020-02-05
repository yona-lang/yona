package yatta;

import org.graalvm.polyglot.Value;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import yatta.ast.generators.GeneratedCollection;

import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static yatta.ast.generators.GeneratedCollection.SEQ;

public class GeneratorsTest extends CommonTest {
  @ParameterizedTest
  @MethodSource("testCases")
  void testGenerator(GeneratorTestCase args) {
    Value ret = context.eval(YattaLanguage.ID, args.toString());
    assertEquals(args.expectedLength, ret.getArraySize());

    Object[] array = ret.as(Object[].class);
    for (int i = 0; i < args.expectedLength; i++) {
      assertEquals(args.expectedValues[i], array[i]);
    }
  }

  static Stream<GeneratorTestCase> testCases() {
    return Stream.of(
        // SEQ -> SEQ
        new GeneratorTestCase(SEQ, "x * 2", "x <- [1, 2, 3]", 3, 2l, 4l, 6l),
        new GeneratorTestCase(SEQ, "x * 2", "x <- async \\-> [1, 2, 3]", 3, 2l, 4l, 6l),
        new GeneratorTestCase(SEQ, "async \\-> x * 2", "x <- [1, 2, 3]", 3, 2l, 4l, 6l),
        new GeneratorTestCase(SEQ, "x * 2", "x <- [1, 2, 3]", "x < 3", 2, 2l, 4l),
        new GeneratorTestCase(SEQ, "x * 2", "x <- [1, 2, 3]", "async \\-> x < 3", 2, 2l, 4l),

        // SET -> SEQ
        new GeneratorTestCase(SEQ, "x * 2", "x <- {1, 2, 3}", 3, 2l, 4l, 6l),
        new GeneratorTestCase(SEQ, "x * 2", "x <- async \\-> {1, 2, 3}", 3, 2l, 4l, 6l),
        new GeneratorTestCase(SEQ, "async \\-> x * 2", "x <- {1, 2, 3}", 3, 2l, 4l, 6l),
        new GeneratorTestCase(SEQ, "x * 2", "x <- {1, 2, 3}", "x < 3", 2, 2l, 4l),
        new GeneratorTestCase(SEQ, "x * 2", "x <- {1, 2, 3}", "async \\-> x < 3", 2, 2l, 4l),

        // DICT -> SEQ
        new GeneratorTestCase(SEQ, "k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", 3, 2l, 6l, 12l),
        new GeneratorTestCase(SEQ, "k * v", "k = v <- async \\-> {1 = 2, 2 = 3, 3 = 4}", 3, 2l, 6l, 12l),
        new GeneratorTestCase(SEQ, "async \\-> k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", 3, 2l, 6l, 12l),
        new GeneratorTestCase(SEQ, "k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "v < 4", 2, 2l, 6l),
        new GeneratorTestCase(SEQ, "k * v", "k = v <- {1 = 2, 2 = 3, 3 = 4}", "async \\-> v < 4", 2, 2l, 6l)
    );
  }

  private static class GeneratorTestCase {
    final String reducer, innerCollection, condition;
    final int expectedLength;
    final Object[] expectedValues;
    final GeneratedCollection type;

    public GeneratorTestCase(GeneratedCollection type, String reducer, String innerCollection, int expectedLength, Object... expectedValues) {
      this.type = type;
      this.reducer = reducer;
      this.innerCollection = innerCollection;
      this.expectedLength = expectedLength;
      this.expectedValues = expectedValues;
      this.condition = null;
    }

    public GeneratorTestCase(GeneratedCollection type, String reducer, String innerCollection, String condition, int expectedLength, Object... expectedValues) {
      this.type = type;
      this.reducer = reducer;
      this.innerCollection = innerCollection;
      this.condition = condition;
      this.expectedLength = expectedLength;
      this.expectedValues = expectedValues;
    }

    @Override
    public String toString() {
      String body = reducer + " | " + innerCollection + (condition == null ? "" : " if " + condition);
      switch (type) {
        case SEQ: return "[" + body + "]";
        case SET: return "{" + body + "}";
        case DICT: return "{" + body + "}";
      }
      return null;
    }
  }
}
