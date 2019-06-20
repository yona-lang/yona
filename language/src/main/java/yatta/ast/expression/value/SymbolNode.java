package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.runtime.Context;
import yatta.runtime.Symbol;

import java.util.Objects;

@NodeInfo
public final class SymbolNode extends ExpressionNode {
  public final Symbol value;

  public SymbolNode(String value) {
    this.value = Context.getCurrent().symbol(value);
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
    return value;
  }

  @Override
  public Symbol executeSymbol(VirtualFrame frame) throws UnexpectedResultException {
    return value;
  }
}
