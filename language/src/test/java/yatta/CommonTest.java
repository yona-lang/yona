package yatta;

import org.graalvm.polyglot.Context;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

public abstract class CommonTest {
  protected Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.newBuilder().allowAllAccess(true).environment("YATTA_STDLIB_HOME", "lib-yatta").build();
    context.enter();
  }

  @AfterEach
  public void dispose() {
    context.leave();
  }

  /**
   * This can be used in Context Builder using .option(, "<log level>")
   */
  @SuppressWarnings("unused")
  private String logLevelOption(Class<?> cls) {
    return "log." + YattaLanguage.ID + "." + cls.getName() + ".level";
  }
}
