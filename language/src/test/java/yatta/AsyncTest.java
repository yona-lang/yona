package yatta;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class AsyncTest extends CommonTest {
  @Test
  public void simpleAsyncTest() {
    long ret = context.eval(YattaLanguage.ID, "async \\-> 5").asLong();
    assertEquals(5l, ret);
  }

  @Test
  public void asyncCaseTest() {
    long ret = context.eval(YattaLanguage.ID, "case (async \\-> (1, 2)) of\n" +
        "(1, 3) -> 5\n" +
        "(1, 2) -> 4\n" +
        "_      -> 3\n" +
        "end\n").asLong();
    assertEquals(4l, ret);
  }

  @Test
  public void functionCallWithAsyncArgTest() {
    long ret = context.eval(YattaLanguage.ID, "let fun = \\argone argtwo -> argone + argtwo in fun 1 (async \\-> 2)").asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void functionCallWithMultipleAsyncArgTest() {
    long ret = context.eval(YattaLanguage.ID, "let fun = \\argone argtwo -> argone + argtwo in fun (async \\-> 1) (async \\-> 2)").asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void binaryEqYesAsyncArgTest() {
    boolean ret = context.eval(YattaLanguage.ID, "(async \\-> 2) == (async \\-> 2)").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void binaryEqYesLeftAsyncArgTest() {
    boolean ret = context.eval(YattaLanguage.ID, "(async \\-> 2) == 2").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void binaryEqYesRightAsyncArgTest() {
    boolean ret = context.eval(YattaLanguage.ID, "2 == (async \\-> 2)").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void binaryEqNoAsyncArgTest() {
    boolean ret = context.eval(YattaLanguage.ID, "(async \\-> 1) == (async \\-> 2)").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void binaryEqNoLeftAsyncArgTest() {
    boolean ret = context.eval(YattaLanguage.ID, "(async \\-> 1) == 2").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void binaryEqNoRightAsyncArgTest() {
    boolean ret = context.eval(YattaLanguage.ID, "2 == (async \\-> 1)").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void conditionTrueAsyncArgTest() {
    long ret = context.eval(YattaLanguage.ID, "if (async \\-> true) then 1 else 2").asLong();
    assertEquals(1l, ret);
  }

  @Test
  public void conditionFalseAsyncArgTest() {
    long ret = context.eval(YattaLanguage.ID, "if (async \\-> false) then 1 else 2").asLong();
    assertEquals(2l, ret);
  }

  @Test
  public void asyncPatternLetTest() {
    long ret = context.eval(YattaLanguage.ID, "let\n" +
        "(1, x) = async \\-> (1, 2)\n" +
        "y      = async \\-> x - 1\n" +
        "in y").asLong();
    assertEquals(1l, ret);
  }

  @Test
  public void simpleLetTest() {
    long ret = context.eval(YattaLanguage.ID, "let\n" +
        "x = async \\-> 1\n" +
        "y = async \\-> 2\n" +
        "z = x + y\n" +
        "in z").asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void simpleDoTest() {
    long ret = context.eval(YattaLanguage.ID, "do\n" +
        "one = async \\-> 1\n" +
        "println one\n" +
        "two = async \\-> 2\n" +
        "one + two\n" +
        "end\n").asLong();
    assertEquals(3l, ret);
  }

  @Test
  public void simpleTryCatchTest() {
    String ret = context.eval(YattaLanguage.ID, "try\n" +
        "async \\-> raise :random_error \"something happened\"\n" +
        "catch\n" +
        "(:not_this, _, _) -> \"nothing\"\n" +
        "(:random_error, message, stacktrace) -> message\n" +
        "end\n").asString();

    assertEquals("YattaError <random_error>: something happened", ret);
  }

  @Test
  public void functionAsAPromise() {
    long ret = context.eval(YattaLanguage.ID, "let\n" +
        "   fun = \\a b -> a + b\n" +
        "   afun = async \\-> fun\n" +
        "in afun 1 2").asLong();
    assertEquals(3l, ret);
  }
}
