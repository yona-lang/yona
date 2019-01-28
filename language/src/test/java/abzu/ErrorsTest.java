package abzu;

import abzu.ast.pattern.MatchException;
import abzu.runtime.Tuple;
import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class ErrorsTest {
  private Context context;

  @Before
  public void initEngine() {
    context = Context.create();
  }

  @After
  public void dispose() {
    context.close();
  }

  @Test(expected = PolyglotException.class)
  public void oneArgFunctionTest() {
    try {
      context.eval("abzu", "fun arg = argx").execute(6);
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Identifier 'argx' not found in the current scope");
      throw ex;
    }
  }

  @Test(expected = PolyglotException.class)
  public void invocationInLet1Test() {
    try {
      context.eval("abzu", "let " +
          "funone := \\arg -> arg " +
          "alias := 6" +
          "funalias := \\arg -> funoneX alias " +
          "in funalias").execute(5).asLong();
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Identifier 'funoneX' not found in the current scope");
      throw ex;
    }
  }

  @Test(expected = PolyglotException.class)
  public void invocationInLet2Test() {
    try {
      context.eval("abzu", "let " +
          "funone := \\arg -> arg " +
          "alias := 6" +
          "funalias := \\arg -> funoneX alias " +
          "in whatever").execute(5).asLong();
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Identifier 'whatever' not found in the current scope");
      throw ex;
    }
  }

  @Test(expected = PolyglotException.class)
  public void invalidNumberOfArgsTest() {
    try {
      context.eval("abzu", "let " +
          "funone := \\arg -> arg " +
          "alias := 6" +
          "funalias := \\arg -> funone alias 7 " +
          "in funalias").execute(5).asLong();
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Unexpected number of arguments when calling '$lambda-0': 2 expected: 1");
      throw ex;
    }
  }

  @Test(expected = PolyglotException.class)
  public void callOfNonFunctionTest() {
    try {
      context.eval("abzu", "let " +
          "funone := \\arg -> arg " +
          "alias := 6" +
          "funalias := \\arg -> alias 7 funone " +
          "in funalias").execute(5).asLong();
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Cannot invoke non-function node: IdentifierNode{name='alias'}");
      throw ex;
    }
  }

  @Test(expected = PolyglotException.class)
  public void callOfPrivateModuleFunctionTest() {
    try {
      context.eval("abzu", "let\n" +
          "testMod := module testMod exports funone as\n" +
          "funone argone = funtwo argone\n" +
          "funtwo argone = argone\n" +
          "in testMod.funtwo 6").asLong();
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Function funtwo is not present in Module{fqn=[testMod], exports=[funone], functions={funone=funone, funtwo=funtwo}}");
      throw ex;
    }
  }

  @Test(expected = PolyglotException.class)
  public void freeVarOverridePatternTest() {
    try {
      context.eval("abzu", "fun arg = case arg of\n" +
          "(1, 2, 3) -> 6\n" +
          "(1, secondArg, 3) -> 2 + secondArg\n" +
          "(1, _, _) -> 1 + secondArg\n" +
          "(2, 3) -> 5\n" +
          "_ -> 9\n").execute(new Tuple(1l, 5l, 6l)).asLong();
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Identifier 'secondArg' not found in the current scope");
      throw ex;
    }
  }

  @Test(expected = PolyglotException.class)
  public void simpleIntNoMatchPatternTest() {
    try {
      context.eval("abzu", "fun arg = case arg of\n" +
          "1 -> 2\n" +
          "2 -> 3\n").execute(3l).asLong();
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), MatchException.class.getCanonicalName());
      throw ex;
    }
  }
}
