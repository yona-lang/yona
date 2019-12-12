package yatta.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.runtime.Symbol;

import java.util.Objects;

@NodeInfo(shortName = "cached-symbol")
public final class CachedSymbolNode extends SymbolNode {
  public final Symbol symbol;

  public CachedSymbolNode(Symbol symbol) {
    this.symbol = symbol;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    CachedSymbolNode that = (CachedSymbolNode) o;
    return Objects.equals(symbol, that.symbol);
  }

  @Override
  public int hashCode() {
    return Objects.hash(symbol);
  }

  @Override
  public String toString() {
    return "CachedSymbolNode{" +
        "symbol=" + symbol +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return symbol;
  }

  @Override
  public Symbol executeSymbol(VirtualFrame frame) throws UnexpectedResultException {
    return symbol;
  }
}
