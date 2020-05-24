package yatta.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Objects;

@NodeInfo
public final class CharacterNode extends LiteralValueNode {
  public final int value;

  public CharacterNode(int value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    CharacterNode integerNode = (CharacterNode) o;
    return Objects.equals(value, integerNode.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "CharacterNode{" +
        "value=" + new String(new int[] {value}, 0, 1) +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return value;
  }

  @Override
  public int executeInteger(VirtualFrame frame) throws UnexpectedResultException {
    return value;
  }
}
