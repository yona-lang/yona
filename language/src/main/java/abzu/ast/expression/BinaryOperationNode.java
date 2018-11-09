package abzu.ast.expression;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.Objects;

public final class BinaryOperationNode extends ExpressionNode {
  @Node.Child
  public ExpressionNode left;
  @Node.Child
  public ExpressionNode right;
  public final String op;

  public BinaryOperationNode(ExpressionNode left, ExpressionNode right, String op) {
    this.left = left;
    this.right = right;
    this.op = op;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    BinaryOperationNode that = (BinaryOperationNode) o;
    return Objects.equals(left, that.left) &&
        Objects.equals(right, that.right) &&
        Objects.equals(op, that.op);
  }

  @Override
  public int hashCode() {
    return Objects.hash(left, right, op);
  }

  @Override
  public String toString() {
    return "BinaryOperationNode{" +
        "left=" + left +
        ", right=" + right +
        ", op='" + op + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }
}
