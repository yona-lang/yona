package abzu.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.expression.ValueNode;

import java.util.Objects;

@NodeInfo
public final class FloatNode extends ValueNode<Double> {
  public final double value;

  public FloatNode(Double value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FloatNode that = (FloatNode) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "FloatNode{" +
        "value=" + value +
        '}';
  }

  @Override
  public Double executeValue(VirtualFrame frame) {
    return value;
  }
}
