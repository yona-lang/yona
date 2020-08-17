package yona.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.YonaException;
import yona.YonaSymbolException;
import yona.ast.ExpressionNode;
import yona.ast.expression.value.SymbolNode;
import yona.runtime.DependencyUtils;
import yona.runtime.Seq;
import yona.runtime.Symbol;
import yona.runtime.async.Promise;

import java.util.Objects;

@NodeInfo(shortName = "throwYona")
public final class ThrowYonaExceptionNode extends ExpressionNode {
  @Child
  private SymbolNode symbolNode;
  @Child
  private ExpressionNode stringNode;  // StringLiteralNode | StringInterpolationNode

  public ThrowYonaExceptionNode(SymbolNode symbolNode, ExpressionNode stringNode) {
    this.symbolNode = symbolNode;
    this.stringNode = stringNode;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ThrowYonaExceptionNode that = (ThrowYonaExceptionNode) o;
    return Objects.equals(symbolNode, that.symbolNode) &&
        Objects.equals(stringNode, that.stringNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(symbolNode, stringNode);
  }

  @Override
  public String toString() {
    return "ThrowYonaExceptionNode{" +
        "symbolNode=" + symbolNode +
        ", stringNode=" + stringNode +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      Symbol evaluatedSymbol = symbolNode.executeSymbol(frame);
      Object evaluatedString = stringNode.executeGeneric(frame);
      if (evaluatedString instanceof Seq) {
        throw new YonaSymbolException((Seq) evaluatedString, this, evaluatedSymbol);
      } else {
        assert evaluatedString instanceof Promise;
        Promise promise = (Promise) evaluatedString;
        return promise.map(message -> new YonaSymbolException((Seq) message, this, evaluatedSymbol), this);
      }
    } catch (UnexpectedResultException ex) {
      throw new YonaException("Unexpected error while constructing an Exception: " + ex.getMessage(), this);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(symbolNode, stringNode);
  }
}
