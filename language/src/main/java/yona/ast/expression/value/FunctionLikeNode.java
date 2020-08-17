package yona.ast.expression.value;

import yona.ast.ExpressionNode;

public abstract class FunctionLikeNode extends ExpressionNode {
  public abstract String name();
}
