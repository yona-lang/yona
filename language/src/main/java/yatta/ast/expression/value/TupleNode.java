package yatta.ast.expression.value;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.ast.ExpressionNode;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;

import java.util.Arrays;

@NodeInfo
public final class TupleNode extends ExpressionNode {
  @Node.Children
  public final ExpressionNode[] expressions;

  public TupleNode(ExpressionNode... expressions) {
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
    CompilerAsserts.compilationConstant(expressions.length);
    Object[] results = new Object[expressions.length];
    boolean containsPromise = false;

    for (int i = 0; i < expressions.length; i++) {
      results[i] = expressions[i].executeGeneric(frame);
      if (results[i] instanceof Promise) {
        containsPromise = true;
      }
    }

    if (containsPromise) {
      return Promise.all(results, this).map(res -> new Tuple((Object[]) res), this);
    } else {
      return new Tuple(results);
    }
  }
}
