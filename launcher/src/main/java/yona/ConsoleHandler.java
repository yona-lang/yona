package yona;

import org.graalvm.polyglot.Context;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * The interface to a source of input/output for the context, which may have different
 * implementations for different contexts.
 */
public abstract class ConsoleHandler {

  /**
   * Read a line of input, newline is <b>NOT</b> included in result.
   */
  public final String readLine() {
    return readLine(true);
  }

  public abstract String readLine(boolean prompt);

  public abstract void setPrompt(String prompt);

  public void setContext(@SuppressWarnings("unused") Context context) {
  }

  public InputStream createInputStream() {
    return new InputStream() {
      byte[] buffer = null;
      int pos = 0;

      @Override
      public int read() throws IOException {
        if (pos < 0) {
          pos = 0;
          return -1;
        } else if (buffer == null) {
          assert pos == 0;
          String line = readLine(false);
          if (line == null) {
            return -1;
          }
          buffer = line.getBytes(StandardCharsets.UTF_8);
        }
        if (pos == buffer.length) {
          buffer = null;
          pos = -1;
          return '\n';
        } else {
          return buffer[pos++];
        }
      }
    };
  }

  public int getTerminalWidth() {
    return 80;
  }

  public int getTerminalHeight() {
    return 25;
  }

  public void saveHistory() {}
}
