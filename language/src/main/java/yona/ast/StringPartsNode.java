package yona.ast;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.runtime.DependencyUtils;
import yona.runtime.Seq;
import yona.runtime.async.Promise;

import java.util.Arrays;

@NodeInfo(shortName = "stringParts")
public final class StringPartsNode extends ExpressionNode {
  @Children
  private ExpressionNode[] expressionNodes;

  public StringPartsNode(ExpressionNode[] expressionNodes) {
    this.expressionNodes = expressionNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    StringPartsNode that = (StringPartsNode) o;
    return Arrays.equals(expressionNodes, that.expressionNodes);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(expressionNodes);
  }

  @Override
  public String toString() {
    return "StringPartsNode{" +
        "expressionNodes=" + Arrays.toString(expressionNodes) +
        '}';
  }

  @Override
  @ExplodeLoop
  public Object executeGeneric(VirtualFrame frame) {
    CompilerAsserts.compilationConstant(expressionNodes.length);
    Object[] evaluatedExpressions = new Object[expressionNodes.length];
    boolean isPromise = false;

    for (int i = 0; i < expressionNodes.length; i++) {
      evaluatedExpressions[i] = expressionNodes[i].executeGeneric(frame);
      if (evaluatedExpressions[i] instanceof Promise) {
        isPromise = true;
      } else {
        assert evaluatedExpressions[i] instanceof Seq;
      }
    }

    CompilerDirectives.transferToInterpreterAndInvalidate();
    if (!isPromise) {
      Seq sb = Seq.EMPTY;

      for (Object evaluatedExpression : evaluatedExpressions) {
        sb = Seq.catenate(sb, (Seq) evaluatedExpression);
      }

      return sb;
    } else {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      return Promise.all(evaluatedExpressions, this).map(vals -> {
        Seq sb = Seq.EMPTY;
        for (Object val : (Object[]) vals) {
          sb = Seq.catenate(sb, (Seq) val);
        }

        return sb;
      }, this);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(expressionNodes);
  }
}
