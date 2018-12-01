package abzu;

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

  @Test
  public void oneArgFunctionTest() {
    try {
      context.eval("abzu", "fun arg = argx").execute(6);
    } catch (PolyglotException ex) {
      assertEquals(ex.getMessage(), "Identifier 'argx' not found in the current scope");
    }
  }
}
