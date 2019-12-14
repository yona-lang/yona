package yatta;

import org.graalvm.polyglot.Context;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

public abstract class CommonTest {
  protected Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.newBuilder().allowAllAccess(true).build();
    context.enter();
  }

  @AfterEach
  public void dispose() {
    context.leave();
  }
}
