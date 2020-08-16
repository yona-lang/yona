package yatta.parser;

import com.oracle.truffle.api.TruffleException;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;

public final class ParseError extends RuntimeException implements TruffleException {
  private final Source source;
  private final int line;
  private final int column;
  private final int length;

  public ParseError(Source source, int line, int column, int length, String message) {
    super(message);
    this.source = source;
    this.line = line;
    this.column = column;
    this.length = length;
  }

  @Override
  public SourceSection getSourceLocation() {
    return source.createSection(line, column, length);
  }

  @Override
  public Node getLocation() {
    return null;
  }

  @Override
  public boolean isSyntaxError() {
    return true;
  }
}
