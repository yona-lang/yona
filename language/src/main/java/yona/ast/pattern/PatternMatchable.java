package yona.ast.pattern;

import yona.ast.ExpressionNode;

public abstract class PatternMatchable extends ExpressionNode {
  protected Object value;

  public void setValue(Object value) {
    this.value = value;
  }
}
