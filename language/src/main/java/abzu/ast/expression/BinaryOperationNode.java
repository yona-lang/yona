package abzu.ast.expression;

import abzu.AbzuException;
import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

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
    try {
      try {
        long leftVal = left.executeLong(frame);
        long rightVal = right.executeLong(frame);

        switch (op) {
          case "+": return leftVal + rightVal;
          case "-": return leftVal - rightVal;
          case "*": return leftVal * rightVal;
          case "/": return leftVal / rightVal;
          case "%": return leftVal % rightVal;
        }
      } catch (UnexpectedResultException ex) {
        double leftVal = left.executeDouble(frame);
        double rightVal = right.executeDouble(frame);

        switch (op) {
          case "+": return leftVal + rightVal;
          case "-": return leftVal - rightVal;
          case "*": return leftVal * rightVal;
          case "/": return leftVal / rightVal;
          case "%": return leftVal % rightVal;
        }
      }
    } catch (UnexpectedResultException ex) {
      throw new AbzuException("Unable to perform binary op " + left + " " + op + " " + right, this);
    }
    throw new AbzuException("Unknown binary op " + left + " " + op + " " + right, this);
  }
}
