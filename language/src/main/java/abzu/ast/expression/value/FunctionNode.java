package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.expression.ValueNode;

import java.util.List;
import java.util.Objects;

@NodeInfo
public final class FunctionNode extends ValueNode<String> {
  public final String name;
  public final List<String> arguments;
  @Node.Child
  public ExpressionNode expression;

  public FunctionNode(String name, List<String> arguments, ExpressionNode expression) {
    this.name = name;
    this.arguments = arguments;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FunctionNode that = (FunctionNode) o;
    return Objects.equals(name, that.name) &&
        Objects.equals(arguments, that.arguments) &&
        Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, arguments, expression);
  }

  @Override
  public String toString() {
    return "FunctionNode{" +
        "name='" + name + '\'' +
        ", arguments=" + arguments +
        ", expression=" + expression +
        '}';
  }

  @Override
  public String executeValue(VirtualFrame frame) {
    return name;
  }
}
