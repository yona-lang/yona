package abzu.ast.expression.value;

import abzu.ast.AbzuExpressionNode;
import abzu.ast.expression.ValueNode;
import abzu.runtime.AbzuTuple;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class TupleNode extends ValueNode<AbzuTuple> {
  @Node.Children
  public final AbzuExpressionNode[] expressions;

  public TupleNode(AbzuExpressionNode[] expressions) {
    this.expressions = expressions;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    TupleNode tupleNode = (TupleNode) o;
    return Objects.equals(expressions, tupleNode.expressions);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expressions);
  }

  @Override
  public String toString() {
    return "TupleNode{" +
        "expressions=" + expressions +
        '}';
  }

  @Override
  public AbzuTuple executeValue(VirtualFrame frame) {
    return new AbzuTuple(Arrays.stream(expressions).map((el) -> el.executeGeneric(frame)).toArray());
  }
}
