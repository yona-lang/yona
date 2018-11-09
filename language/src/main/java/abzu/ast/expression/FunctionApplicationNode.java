package abzu.ast.expression;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.Objects;

public class FunctionApplicationNode extends ExpressionNode {
  public final String name;
  @Node.Children
  public final ExpressionNode[] arguments;

  public FunctionApplicationNode(String name, ExpressionNode[] arguments) {
    this.name = name;
    this.arguments = arguments;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FunctionApplicationNode that = (FunctionApplicationNode) o;
    return Objects.equals(name, that.name) &&
        Objects.equals(arguments, that.arguments);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, arguments);
  }

  @Override
  public String toString() {
    return "FunctionApplicationNode{" +
        "name='" + name + '\'' +
        ", arguments=" + arguments +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }
}
