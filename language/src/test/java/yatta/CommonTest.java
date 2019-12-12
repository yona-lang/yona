package yatta;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

public abstract class CommonTest {
  protected Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.newBuilder().allowAllAccess(true).build();
  }

  @AfterEach
  public void dispose() {
    try {
      context.close();
    } catch (PolyglotException e) {
      // TODO: No idea why this is needed. It started happening as of 19.3.0, and random test cases fail due to:
      // java.lang.IllegalStateException: The language did not complete all polyglot threads but should have:
      // [Thread[Polyglot-yatta-2063,5,main], Thread[Polyglot-yatta-2061,5,main], Thread[Polyglot-yatta-2062,5,main]]
      e.printStackTrace();
    }
  }
}
