package abzu.ast.expression.value;

import abzu.AbzuLanguage;
import abzu.ast.AbzuRootNode;
import abzu.ast.ExpressionNode;
import abzu.runtime.Function;
import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;

import java.util.List;
import java.util.Objects;

@NodeInfo
public final class FunctionNode extends ExpressionNode {
  public final String name;
  public final List<String> arguments;
  @Node.Child
  public ExpressionNode expression;

  private AbzuLanguage language;
  private SourceSection sourceSection;

  public FunctionNode(AbzuLanguage language, SourceSection sourceSection, String name, List<String> arguments, ExpressionNode expression) {
    this.name = name;
    this.arguments = arguments;
    this.expression = expression;
    this.language = language;
    this.sourceSection = sourceSection;
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
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Function executeFunction(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Function execute(VirtualFrame frame) {
    Function function = new Function(language, name);
    AbzuRootNode rootNode = new AbzuRootNode(language, new FrameDescriptor(), expression, sourceSection, name);
    function.setCallTarget(Truffle.getRuntime().createCallTarget(rootNode));

    return function;
  }
}
