package yatta;

import org.graalvm.polyglot.Context;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class StringTest {
  private Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.newBuilder().allowAllAccess(true).build();
  }

  @AfterEach
  public void dispose() {
    context.close();
  }

  @Test
  void testSimpleInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let who = \"world\" in \"hello {who}\"").asString();
    assertEquals("hello world", ret);
  }

  @Test
  void testSimpleInterpolation2() {
    String ret = context.eval(YattaLanguage.ID, "let xyz = 5 in \"number: {xyz}\"").asString();
    assertEquals("number: 5", ret);
  }

  @Test
  void testSimpleInterpolation3() {
    String ret = context.eval(YattaLanguage.ID, "\"hello {\"world\"}\"").asString();
    assertEquals("hello world", ret);
  }

  @Test
  void testAlignmentInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let var = \"abcde\" in \"{var,10}\"").asString();
    assertEquals("     abcde", ret);
  }

  @Test
  void testAlignmentWithAVarInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let\n" +
        "    var = \"abcde\"\n" +
        "    align = 10\n" +
        "in \n" +
        "    \"{var,align}\"").asString();
    assertEquals("     abcde", ret);
  }

  @Test
  void testNegativeAlignmentInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let var = \"abcde\" in \"{var,-10}\"").asString();
    assertEquals("abcde     ", ret);
  }

  @Test
  void testSimplePromiseInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let\n" +
        "    who = async \\->\"world\"\n" +
        "in\n" +
        "    \"hello {who}\"").asString();
    assertEquals("hello world", ret);
  }

  @Test
  void testAlignmentPromiseInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let\n" +
        "    var = async \\->\"abcde\"\n" +
        "in\n" +
        "    \"{var,(async \\-> 10)}\"").asString();
    assertEquals("     abcde", ret);
  }

  @Test
  void testNegativeAlignmentPromiseInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let var = \"abcde\" in \"{(async \\->var),(async \\->-10)}\"").asString();
    assertEquals("abcde     ", ret);
  }

  @Test
  void testMultiplePromiseInterpolation() {
    String ret = context.eval(YattaLanguage.ID, "let var = \"abcde\" in\n" +
        "\"{(async \\->var),(async \\->-10)}:{(async \\->var),(async \\->10)}\"").asString();
    assertEquals("abcde     :     abcde", ret);
  }

  @Test
  void testMultiline() {
    String ret = context.eval(YattaLanguage.ID, "\"hello\nworld\"").asString();
    assertEquals("hello\nworld", ret);
  }
}
