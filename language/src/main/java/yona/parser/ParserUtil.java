package yona.parser;

import org.antlr.v4.runtime.Token;
import com.oracle.truffle.api.source.Source;

public final class ParserUtil {
  public static String positionOfToken(Token token, String tokenName, Source source) {
    return ("\n\t%s: '%s'. File %s, line: %d, column: %d").formatted(tokenName, token.getText(), source.getPath(), token.getLine(), token.getCharPositionInLine());
  }
}
