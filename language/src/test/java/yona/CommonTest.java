package yona;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.Source;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.extension.ExtendWith;

import java.io.IOException;

@ExtendWith(LoggingExtension.class)
public abstract class CommonTest {
  protected static Context context;

  @BeforeAll
  public static void initEngine() {
    context = Context.newBuilder().allowAllAccess(true).environment("YONA_STDLIB_HOME", "lib-yona").build();
  }

  @AfterAll
  public static void dispose() {
    try {
      context.eval(Source.newBuilder(YonaLanguage.ID, "shutdown", "shutdown").internal(true).build());
    } catch (IOException ignored) {
    }
    context.close();
  }

  /**
   * This can be used in Context Builder using .option(, "<log level>")
   */
  @SuppressWarnings("unused")
  static String logLevelOption(Class<?> cls) {
    return "log." + YonaLanguage.ID + "." + cls.getName() + ".level";
  }
}
