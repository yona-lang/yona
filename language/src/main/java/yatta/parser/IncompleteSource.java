package yatta.parser;

import com.oracle.truffle.api.TruffleException;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;

public final class IncompleteSource extends RuntimeException implements TruffleException {
  private Source source;
  private final int line;

  public IncompleteSource(Source source, String message, Throwable cause, int line) {
    super(message, cause);
    this.source = source;
    this.line = line;
  }

  @Override
  public Node getLocation() {
    if (line <= 0 || line > source.getLineCount()) {
      return null;
    } else {
      SourceSection section = source.createSection(line);
      return new Node() {
        @Override
        public SourceSection getSourceSection() {
          return section;
        }
      };
    }
  }

  @Override
  public boolean isSyntaxError() {
    return true;
  }

  @Override
  public boolean isIncompleteSource() {
    return true;
  }

  public void setSource(Source source) {
    this.source = source;
  }
}
