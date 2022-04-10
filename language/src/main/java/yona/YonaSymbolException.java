package yona;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Seq;
import yona.runtime.Symbol;
import yona.runtime.Tuple;

public class YonaSymbolException extends YonaException {
  public final Symbol symbol;

  public YonaSymbolException(String message, Node location, Symbol symbol) {
    super(message, location);
    this.symbol = symbol;
  }

  public YonaSymbolException(Seq message, Node location, Symbol symbol) {
    this(message.asJavaString(location), location, symbol);
  }

  @Override
  @CompilerDirectives.TruffleBoundary
  public Tuple asTuple() {
    return Tuple.allocate(null, symbol, getMessage(), stacktraceToSequence(this, null));
  }

  @Override
  public String getMessage() {
    return "YonaError <" + symbol.asString() + ">: " + super.getMessage();
  }
}
