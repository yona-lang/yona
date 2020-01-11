package yatta.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.ast.ExpressionNode;
import yatta.runtime.Murmur3;
import yatta.runtime.Set;

import java.util.Arrays;

@NodeInfo
public class SetNode extends ExpressionNode {
  @Node.Children
  public final ExpressionNode[] expressions;

  public SetNode(ExpressionNode[] expressions) {
    this.expressions = expressions;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    SetNode setNode = (SetNode) o;
    return Arrays.equals(expressions, setNode.expressions);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(expressions);
  }

  @Override
  public String toString() {
    return "SetNode{" +
        "expressions=" + Arrays.toString(expressions) +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Set executeSet(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Set execute(VirtualFrame frame) {
    Set set = Set.empty(Murmur3.INSTANCE, 0l);
    for (ExpressionNode expressionNode : expressions) {
      set = set.add(expressionNode.executeGeneric(frame));
    }

    return set;
  }
}
