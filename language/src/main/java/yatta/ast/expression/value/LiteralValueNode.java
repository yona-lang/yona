package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;

public abstract class LiteralValueNode extends ExpressionNode {
  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }
}
