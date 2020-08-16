package yatta.parser;

import com.oracle.truffle.api.source.Source;
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.misc.IntervalSet;

import java.util.Collections;
import java.util.List;

public class YattaErrorStrategy extends DefaultErrorStrategy {
  private final Source source;

  public YattaErrorStrategy(Source source) {
    this.source = source;
  }

  @Override
  public void reportNoViableAlternative(Parser parser, NoViableAltException e) throws RecognitionException {
    Recognizer<?, ?> recognizer = e.getRecognizer();
    StringBuilder msg = new StringBuilder();
    List<String> stack = parser.getRuleInvocationStack();
    IntervalSet expectedTokens = e.getExpectedTokens();

    Collections.reverse(stack);

    msg.append("can't choose next alternative. Parser stack: [");

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
    if (et.contains(YattaParser.KW_END) || et.contains(YattaParser.NEWLINE)) {
      throw new IncompleteSource(source, message, cause, line);
    }
  }
}
