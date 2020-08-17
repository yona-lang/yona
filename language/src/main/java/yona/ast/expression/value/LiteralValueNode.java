package yona.ast.expression.value;

import yona.ast.ExpressionNode;

public abstract class LiteralValueNode extends ExpressionNode {
  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }
}
