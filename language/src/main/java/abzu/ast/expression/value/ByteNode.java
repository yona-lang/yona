package abzu.ast.expression.value;

import abzu.ast.expression.ValueNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Objects;

@NodeInfo
public final class ByteNode extends ValueNode<Byte> {
  public final Byte value;

  public ByteNode(Byte value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ByteNode byteNode = (ByteNode) o;
    return Objects.equals(value, byteNode.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "ByteNode{" +
        "value=" + value +
        '}';
  }

  @Override
  public Byte executeValue(VirtualFrame frame) {
    return value;
  }
}
