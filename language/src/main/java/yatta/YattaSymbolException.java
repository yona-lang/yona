package yatta;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.Symbol;
import yatta.runtime.Tuple;

public class YattaSymbolException extends YattaException {
  private final Symbol symbol;

  public YattaSymbolException(String message, Node location, Symbol symbol) {
    super(message, location);
    this.symbol = symbol;
  }

  @Override
  @CompilerDirectives.TruffleBoundary
  public Tuple asTuple() {
    return new Tuple(symbol, getMessage(), stacktraceToSequence(this));
  }

  @Override
  public String getMessage() {
    return "YattaError <" + symbol.asString() + ">: " + super.getMessage();
  }
}
