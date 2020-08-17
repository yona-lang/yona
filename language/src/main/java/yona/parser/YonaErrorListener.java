package yona.parser;

import com.oracle.truffle.api.source.Source;
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.misc.Interval;

public class YonaErrorListener extends BaseErrorListener {
  private final Source source;

  public YonaErrorListener(Source source) {
    this.source = source;
  }

  @Override
  public void syntaxError(Recognizer<?, ?> recognizer, Object offendingSymbol, int line, int charPositionInLine, String msg, RecognitionException e) {
    throwParseError(source, recognizer, (Token) offendingSymbol, line, charPositionInLine, msg);
  }

  private static void throwParseError(Source source, Recognizer<?, ?> recognizer, Token offendingToken, int line, int charPositionInLine, String syntaxErrMsg) {
    StringBuilder msg = new StringBuilder();
    msg.append("Error(s) parsing script:\n");
    msg.append("-- line ");
    msg.append(line);
    msg.append(": ");
    msg.append(charPositionInLine);
    msg.append(" ");
    msg.append(syntaxErrMsg);
    msg.append("\n");
    String input;
    if (recognizer.getInputStream() instanceof TokenStream) {
      TokenStream tokens = (TokenStream) recognizer.getInputStream();
      input = tokens.getTokenSource().getInputStream().toString();
    } else {
      CharStream tokens = (CharStream) recognizer.getInputStream();
      input = tokens.getText(Interval.of(0, tokens.size()));
    }
    String[] lines = input.split("\n");
    String errorLine = lines.length == 1 ? lines[0] : lines[line - 1];
    msg.append(errorLine);
    msg.append("\n");
    for (int i = 0; i < charPositionInLine; i++) {
      msg.append(" ");
    }
    int length;
    if (offendingToken != null) {
      int start = offendingToken.getStartIndex();
      int stop = offendingToken.getStopIndex();
      if (start >= 0 && stop >= 0) {
        for (int i = start; i <= stop; i++) {
          msg.append("^");
        }
      }
      length = Math.max(stop - start, 0);
    } else {
      length = 0;
    }
    throw new ParseError(source, line, charPositionInLine + 1, length, msg.toString());
  }
}
