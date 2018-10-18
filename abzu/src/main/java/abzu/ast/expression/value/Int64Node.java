package abzu.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.expression.ValueNode;

import java.util.Objects;

@NodeInfo
public final class Int64Node extends ValueNode<Long> {
  public final Long value;

  public Int64Node(Long value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    Int64Node int64Node = (Int64Node) o;
    return Objects.equals(value, int64Node.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "Int64Node{" +
        "value=" + value +
        '}';
  }

  @Override
  public Long executeValue(VirtualFrame frame) {
    return value;
  }
}
