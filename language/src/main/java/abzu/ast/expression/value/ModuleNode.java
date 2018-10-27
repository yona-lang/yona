package abzu.ast.expression.value;

import abzu.ast.AbzuExpressionNode;
import abzu.ast.expression.ValueNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Objects;

@NodeInfo
public final class ModuleNode extends ValueNode {
  @Node.Child
  public FQNNode fqn;
  @Node.Child
  public NonEmptyStringListNode exports;
  @Node.Children
  public FunctionNode[] functions;

  @Child
  public AbzuExpressionNode expression;

  public ModuleNode(FQNNode fqn, NonEmptyStringListNode exports, FunctionNode[] functions) {
    this.fqn = fqn;
    this.exports = exports;
    this.functions = functions;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ModuleNode that = (ModuleNode) o;
    return Objects.equals(fqn, that.fqn) &&
        Objects.equals(exports, that.exports) &&
        Objects.equals(functions, that.functions);
  }

  @Override
  public int hashCode() {
    return Objects.hash(fqn, exports, expression);
  }

  @Override
  public String toString() {
    return "ModuleNode{" +
        "name='" + fqn + '\'' +
        ", exports=" + exports +
        ", functions=" + functions +
        '}';
  }

  @Override
  public Objects executeValue(VirtualFrame frame) {
    return null;
  }
}
