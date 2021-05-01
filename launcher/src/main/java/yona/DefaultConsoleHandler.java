package yona;

import java.io.*;

public class DefaultConsoleHandler extends ConsoleHandler {

  private final BufferedReader in;
  private final PrintStream out;
  private String prompt;

  public DefaultConsoleHandler(InputStream in, OutputStream out) {
    this.in = new BufferedReader(new InputStreamReader(in));
    this.out = new PrintStream(out);
  }

  @Override
  public String readLine(boolean showPrompt) throws IOException {
    if (prompt != null && showPrompt) {
      out.print(prompt);
    }
    return in.readLine();
  }

  @Override
  public void setPrompt(String prompt) {
    this.prompt = prompt;
  }
}
