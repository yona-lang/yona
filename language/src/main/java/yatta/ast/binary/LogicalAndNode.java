package yatta.ast.binary;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.runtime.DependencyUtils;
import yatta.runtime.async.Promise;

/**
 * This is not a BinaryOpNode, because lazy boolean condition evaluation is required, so arguments must be handled differently
 */
@NodeInfo(shortName = "&&")
public final class LogicalAndNode extends ExpressionNode {
  @Child private ExpressionNode leftNode;
  @Child private ExpressionNode rightNode;

  public LogicalAndNode(ExpressionNode leftNode, ExpressionNode rightNode) {
    this.leftNode = leftNode;
    this.rightNode = rightNode;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object leftValue = leftNode.executeGeneric(frame);
    if (leftValue instanceof Boolean) {
      if (Boolean.FALSE.equals(leftValue)) {
        return false;
      } else {
        return evaluateRight(frame, (Boolean) leftValue);
      }
    } else if (leftValue instanceof Promise) {
      return ((Promise) leftValue).map((evaluatedLeftValue) -> {
        if (evaluatedLeftValue instanceof Boolean) {
          if (Boolean.FALSE.equals(evaluatedLeftValue)) {
            return false;
          } else {
            return evaluateRight(frame, (Boolean) evaluatedLeftValue);
          }
        } else {
          return YattaException.typeError(this, leftValue);
        }
      }, this);
    } else {
      throw YattaException.typeError(this, leftValue);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(leftNode, rightNode);
  }

  private Object evaluateRight(VirtualFrame frame, Boolean leftValue) {
    Object rightValue = rightNode.executeGeneric(frame);
    if (rightValue instanceof Boolean) {
      return (boolean) leftValue && (boolean) rightValue;
    } else if (rightValue instanceof Promise) {
      return ((Promise) rightValue).map((evaluatedRightValue) -> {
        if (evaluatedRightValue instanceof Boolean) {
          if (Boolean.FALSE.equals(evaluatedRightValue)) {
            return false;
          } else {
            return leftValue.booleanValue() && ((Boolean) evaluatedRightValue).booleanValue();
          }
        } else {
          return YattaException.typeError(this, leftValue, evaluatedRightValue);
        }
      }, this);
    } else {
      throw YattaException.typeError(this, leftValue, rightValue);
    }
  }
}
