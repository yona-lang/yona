package yona.parser;

import com.oracle.truffle.api.source.Source;
import org.antlr.v4.runtime.Token;
import org.apache.commons.lang3.text.translate.*;

public final class ParserUtil {
  public static String positionOfToken(Token token, String tokenName, Source source) {
    return ("\n\t%s: '%s'. File %s, line: %d, column: %d").formatted(tokenName, token.getText(), source.getPath(), token.getLine(), token.getCharPositionInLine());
  }

  private static final String[][] YONA_CTRL_CHARS_UNESCAPE = new String[][]{
    {"\\b", "\b"},
    {"\\n", "\n"},
    {"\\t", "\t"},
    {"\\f", "\f"},
    {"\\r", "\r"},
    {"\\0", "\0"}
  };
  public static final CharSequenceTranslator UNESCAPE_YONA =
    new AggregateTranslator(
      new OctalUnescaper(),     // .between('\1', '\377'),
      new UnicodeUnescaper(),
      new LookupTranslator(EntityArrays.JAVA_CTRL_CHARS_UNESCAPE()),
      new LookupTranslator(
        new String[][]{
          {"\\\"", "\""},
          {"\\\\", "\\"},
          {"\\'", "'"},
          {"\\\"", "\""},
          {"{{", "{"},
          {"}}", "}"},
          {"\\a" /*Bell (alert)*/, String.valueOf((char) 7)},
          {"\\v" /*Vertical tab*/, String.valueOf((char) 9)}
        })
    );

  public static String escapeYonaString(CharSequence rawString) {
    return UNESCAPE_YONA.translate(rawString);
  }
}
