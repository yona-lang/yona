package yona.parser;

import com.oracle.truffle.api.source.Source;
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.misc.IntervalSet;

import java.util.Collections;
import java.util.List;

import static yona.parser.ParserUtil.positionOfToken;

public class YonaErrorStrategy extends DefaultErrorStrategy {
  private final Source source;

  public YonaErrorStrategy(Source source) {
    this.source = source;
  }

  @Override
  public void reportNoViableAlternative(Parser parser, NoViableAltException e) throws RecognitionException {
    Recognizer<?, ?> recognizer = e.getRecognizer();
    StringBuilder msg = new StringBuilder();
    List<String> stack = parser.getRuleInvocationStack();
    IntervalSet expectedTokens = e.getExpectedTokens();

    Collections.reverse(stack);

    msg.append("Error(s) parsing script at: " + positionOfToken(parser.getCurrentToken(), parser.getVocabulary().getDisplayName(parser.getCurrentToken().getType()), source));
    msg.append("\n. Reason: * can't choose next alternative. ");
    msg.append("Parser stack: [");

    for (String item : stack) {
      msg.append(item);
      msg.append(", ");
    }
    if (stack.size() > 0) {
      msg.delete(msg.length() - 2, msg.length());
    }
    msg.append("]. Valid alternatives are: ");
    msg.append(expectedTokens.toString(recognizer.getVocabulary()));

    String message = msg.toString();
    handleRecognitionException(expectedTokens, message, e, e.getOffendingToken().getLine());
    parser.notifyErrorListeners(e.getOffendingToken(), message, e);
  }

  private void handleRecognitionException(IntervalSet et, String message, Throwable cause, int line) {
    if (et.contains(YonaParser.KW_END) || et.contains(YonaParser.NEWLINE)) {
      throw new IncompleteSource(source, message, cause, line);
    }
  }
}
