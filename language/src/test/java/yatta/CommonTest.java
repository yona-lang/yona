package yatta;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.Source;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;

import java.io.IOException;

public abstract class CommonTest {
  protected static Context context;

  @BeforeAll
  public static void initEngine() {
    context = Context.newBuilder().allowAllAccess(true).environment("YATTA_STDLIB_HOME", "lib-yatta").build();
  }

  @AfterAll
  public static void dispose() {
    try {
      context.eval(Source.newBuilder(YattaLanguage.ID, "shutdown", "shutdown").internal(true).build());
    } catch (IOException e) {
    }
    context.close();
  }

  /**
   * This can be used in Context Builder using .option(, "<log level>")
   */
  @SuppressWarnings("unused")
  static String logLevelOption(Class<?> cls) {
    return "log." + YattaLanguage.ID + "." + cls.getName() + ".level";
  }
}
