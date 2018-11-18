package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Objects;

@NodeInfo
public final class FQNNode extends ExpressionNode {
  public final String[] parts;

  public FQNNode(String[] parts) {
    this.parts = parts;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FQNNode fqnNode = (FQNNode) o;
    return Objects.equals(parts, fqnNode.parts);
  }

  @Override
  public int hashCode() {
    return Objects.hash(parts);
  }

  @Override
  public String toString() {
    return "FQNNode{" +
        "strings=" + parts +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }
}
