package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import abzu.runtime.Tuple;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Arrays;
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
           "strings=" + Arrays.toString(parts) +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Tuple executeTuple(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Tuple execute(VirtualFrame frame) {
    return new Tuple(parts);
  }
}
