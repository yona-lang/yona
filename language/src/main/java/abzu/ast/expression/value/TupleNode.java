package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import abzu.ast.expression.ValueNode;
import abzu.runtime.Tuple;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class TupleNode extends ValueNode<Tuple> {
  @Node.Children
  public final ExpressionNode[] expressions;

  public TupleNode(ExpressionNode[] expressions) {
    this.expressions = expressions;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    TupleNode tupleNode = (TupleNode) o;
    return Arrays.equals(expressions, tupleNode.expressions);
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
  public Tuple executeValue(VirtualFrame frame) {
    return new Tuple(Arrays.stream(expressions).map((el) -> el.executeGeneric(frame)).toArray());
  }
}
