package yatta.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaSymbolException;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.value.SymbolNode;
import yatta.runtime.Symbol;
import yatta.runtime.async.Promise;

import java.util.Objects;

public class ThrowYattaExceptionNode extends ExpressionNode {
  @Child private SymbolNode symbolNode;
  @Child private ExpressionNode stringNode;  // StringLiteralNode | StringInterpolationNode

  public ThrowYattaExceptionNode(SymbolNode symbolNode, ExpressionNode stringNode) {
    this.symbolNode = symbolNode;
    this.stringNode = stringNode;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ThrowYattaExceptionNode that = (ThrowYattaExceptionNode) o;
    return Objects.equals(symbolNode, that.symbolNode) &&
        Objects.equals(stringNode, that.stringNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(symbolNode, stringNode);
  }

  @Override
  public String toString() {
    return "ThrowYattaExceptionNode{" +
        "symbolNode=" + symbolNode +
        ", stringNode=" + stringNode +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      Symbol evaluatedSymbol = symbolNode.executeSymbol(frame);
      Object evaluatedString = stringNode.executeGeneric(frame);
      if (evaluatedString instanceof String) {
        throw new YattaSymbolException((String) evaluatedString, this, evaluatedSymbol);
      } else {
        assert evaluatedString instanceof Promise;
        Promise promise = (Promise) evaluatedString;
        return promise.map(message -> new YattaSymbolException((String) message, this, evaluatedSymbol), this);
      }
    } catch (UnexpectedResultException ex) {
      throw new YattaException("Unexpected error while constructing an Exception: " + ex.getMessage(), this);
    }
  }
}
