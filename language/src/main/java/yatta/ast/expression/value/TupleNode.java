package yatta.ast.expression.value;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.ast.ExpressionNode;
import yatta.runtime.Tuple;

import java.util.Arrays;

@NodeInfo
public final class TupleNode extends ExpressionNode {
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
    return Arrays.hashCode(expressions);
  }

  @Override
  public String toString() {
    return "TupleNode{" +
        "expressions=" + Arrays.toString(expressions) +
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
    CompilerAsserts.compilationConstant(expressions.length);
    Object[] results = new Object[expressions.length];

    for (int i = 0; i < expressions.length; i++) {
      results[i] = expressions[i].executeGeneric(frame);
    }

    return new Tuple(results);
  }
}
