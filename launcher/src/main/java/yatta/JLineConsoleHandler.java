package yatta;

import org.jline.builtins.Nano;
import org.jline.reader.*;
import org.jline.reader.impl.DefaultHighlighter;
import org.jline.terminal.TerminalBuilder;
import org.jline.utils.AttributedString;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Path;
import java.util.Collection;
import java.util.function.Function;
import java.util.regex.Pattern;

public class JLineConsoleHandler extends ConsoleHandler {
  private static final File HISTORY_FILE = new File(System.getProperty("user.home"), ".yatta_history");

  private final LineReader lineReader;
  private String prompt;

  public JLineConsoleHandler(Path nanorcPath, InputStream inStream, OutputStream outStream, Function<String, Collection<Candidate>> completer) {
    try {
      Nano.SyntaxHighlighter highlighter = Nano.SyntaxHighlighter.build(nanorcPath.toUri().toURL().toString());
      lineReader = LineReaderBuilder.builder()
          .terminal(TerminalBuilder.builder().streams(inStream, outStream).jna(true).system(true).build())
          .variable(LineReader.HISTORY_FILE, HISTORY_FILE)
          .variable(LineReader.INDENTATION, true)
          .variable(LineReader.COMMENT_BEGIN, "#")
          .completer((reader, line, candidates) -> candidates.addAll(completer.apply(line.line())))
          .highlighter(new DefaultHighlighter() {
            @Override
            public AttributedString highlight(LineReader reader, String buffer) {
              return highlighter.highlight(buffer);
            }

            @Override
            public void setErrorPattern(Pattern errorPattern) {
            }

            @Override
            public void setErrorIndex(int errorIndex) {
            }
          })
          .build();
    } catch (IOException ex) {
      throw new RuntimeException("unexpected error opening console reader", ex);
    }
  }

  @Override
  public void saveHistory() {
    try {
      lineReader.getHistory().save();
    } catch (IOException e) {
      e.printStackTrace();
    }
  }

  @Override
  public String readLine(boolean showPrompt) {
    try {
      return lineReader.readLine(showPrompt ? prompt : null);
    } catch (UserInterruptException | EndOfFileException e) {
      return null;
    } catch (Exception ex) {
      throw new RuntimeException(ex);
    }
  }

  @Override
  public void setPrompt(String prompt) {
    this.prompt = prompt;
  }

  @Override
  public int getTerminalHeight() {
    return lineReader.getTerminal().getHeight();
  }

  @Override
  public int getTerminalWidth() {
    return lineReader.getTerminal().getWidth();
  }
}
