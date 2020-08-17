package yona.ast.binary;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.ExpressionNode;
import yona.runtime.DependencyUtils;
import yona.runtime.async.Promise;

/**
 * This is not a BinaryOpNode, because lazy boolean condition evaluation is required, so arguments must be handled differently
 */
@NodeInfo(shortName = "||")
public final class LogicalOrNode extends ExpressionNode {
  @Child
  private ExpressionNode leftNode;
  @Child
  private ExpressionNode rightNode;

  public LogicalOrNode(ExpressionNode leftNode, ExpressionNode rightNode) {
    this.leftNode = leftNode;
    this.rightNode = rightNode;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object leftValue = leftNode.executeGeneric(frame);
    if (leftValue instanceof Boolean) {
      if (Boolean.TRUE.equals(leftValue)) {
        return true;
      } else {
        return evaluateRight(frame);
      }
    } else if (leftValue instanceof Promise) {
      return ((Promise) leftValue).map((evaluatedLeftValue) -> {
        if (evaluatedLeftValue instanceof Boolean) {
          if (Boolean.TRUE.equals(evaluatedLeftValue)) {
            return true;
          } else {
            return evaluateRight(frame);
          }
        } else {
          return YonaException.typeError(this, leftValue);
        }
      }, this);
    } else {
      throw YonaException.typeError(this, leftValue);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(leftNode, rightNode);
  }

  private Object evaluateRight(VirtualFrame frame) {
    Object rightValue = rightNode.executeGeneric(frame);
    if (rightValue instanceof Boolean) {
      return (boolean) rightValue;
    } else if (rightValue instanceof Promise) {
      return ((Promise) rightValue).map((evaluatedRightValue) -> {
        if (evaluatedRightValue instanceof Boolean) {
          if (Boolean.FALSE.equals(evaluatedRightValue)) {
            return false;
          } else {
            return evaluatedRightValue;
          }
        } else {
          return YonaException.typeError(this, evaluatedRightValue);
        }
      }, this);
    } else {
      throw YonaException.typeError(this, rightValue);
    }
  }
}
