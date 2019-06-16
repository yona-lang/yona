package yatta;

import yatta.ast.pattern.MatchException;
import yatta.runtime.Tuple;
import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

import static org.junit.jupiter.api.Assertions.*;

public class ErrorsTest {
  private Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.create();
  }

  @AfterEach
  public void dispose() {
    context.close();
  }

  @Test
  public void oneArgFunctionTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "\\arg -> argx").execute(6);
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "Identifier 'argx' not found in the current scope");
        throw ex;
      }
    });
  }

  @Test
  public void invocationInLet1Test() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "let " +
            "funone = \\arg -> arg " +
            "alias = 6" +
            "funalias = \\arg -> funoneX alias " +
            "in funalias").execute(5).asLong();
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "Identifier 'funoneX' not found in the current scope");
        throw ex;
      }
    });
  }

  @Test
  public void invocationInLet2Test() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "let " +
            "funone = \\arg -> arg " +
            "alias = 6" +
            "funalias = \\arg -> funoneX alias " +
            "in whatever").execute(5).asLong();
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "Identifier 'whatever' not found in the current scope");
        throw ex;
      }
    });
  }

  @Test
  public void invalidNumberOfArgsTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "let " +
            "funone = \\arg -> arg " +
            "alias = 6" +
            "funalias = \\arg -> funone alias 7 " +
            "in funalias").execute(5).asLong();
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "Unexpected number of arguments when calling '$lambda-0': 2 expected: 1");
        throw ex;
      }
    });
  }

  @Test
  public void callOfNonFunctionTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "let " +
            "funone = \\arg -> arg " +
            "alias = 6" +
            "funalias = \\arg -> alias 7 funone " +
            "in funalias").execute(5).asLong();
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "Cannot invoke non-function node: SimpleIdentifierNode{name='alias'}");
        throw ex;
      }
    });
  }

  @Test
  public void callOfPrivateModuleFunctionTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "let\n" +
            "testMod = module TestMod exports funone as\n" +
            "funone argone = funtwo argone\n" +
            "funtwo argone = argone\n" +
            "in testMod.funtwo 6").asLong();
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "Function funtwo is not present in Module{fqn=TestMod, exports=[funone], functions={funone=funone, funtwo=funtwo}}");
        throw ex;
      }
    });
  }

  @Test
  public void freeVarOverridePatternTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
            "(1, 2, 3) -> 6\n" +
            "(1, secondArg, 3) -> 2 + secondArg\n" +
            "(1, _, _) -> 1 + secondArg\n" +
            "(2, 3) -> 5\n" +
            "_ -> 9\n" +
            "end\n").execute(new Tuple(1l, 5l, 6l)).asLong();
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), "Identifier 'secondArg' not found in the current scope");
        throw ex;
      }
    });
  }

  @Test
  public void simpleIntNoMatchPatternTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "\\arg -> case arg of\n" +
            "1 -> 2\n" +
            "2 -> 3\n" +
            "end\n").execute(3l).asLong();
      } catch (PolyglotException ex) {
        assertEquals(ex.getMessage(), MatchException.class.getSimpleName());
        throw ex;
      }
    });
  }
}
