package yatta.ast.expression.value;

import com.oracle.truffle.api.CompilerDirectives;
import yatta.YattaLanguage;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.runtime.Symbol;

import java.util.Objects;

@NodeInfo(shortName = "symbol")
public class SymbolNode extends LiteralValueNode {
  public final String value;

  /**
   * Only to be used by CachedSymbolNode
   */
  SymbolNode() {
    value = null;
  }

  public SymbolNode(String value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    SymbolNode that = (SymbolNode) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "SymbolNode{" +
        "value='" + value + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Symbol executeSymbol(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Symbol execute(VirtualFrame frame) {
    Symbol symbol = lookupContextReference(YattaLanguage.class).get().symbol(value);
    CompilerDirectives.transferToInterpreterAndInvalidate();
    this.replace(new CachedSymbolNode(symbol));
    return symbol;
  }
}
