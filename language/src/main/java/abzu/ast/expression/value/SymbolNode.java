package abzu.ast.expression.value;

import abzu.ast.expression.ValueNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Objects;

@NodeInfo
public final class SymbolNode extends ValueNode<String> {
  public final String value;

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
  public String executeValue(VirtualFrame frame) {
    return value;
  }
}
