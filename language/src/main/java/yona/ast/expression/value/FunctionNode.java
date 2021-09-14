package yona.ast.expression.value;

import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.SourceSection;
import yona.YonaLanguage;
import yona.ast.ClosureRootNode;
import yona.ast.ExpressionNode;
import yona.runtime.Function;

import java.util.Objects;

/**
 * Any function defined not as a module function, lambda
 */
@NodeInfo
public final class FunctionNode extends FunctionLikeNode {
  @Node.Child
  public ExpressionNode expression;

  private final String moduleFQN, name;
  private final int cardinality;

  private final YonaLanguage language;
  private final SourceSection sourceSection;
  private final FrameDescriptor frameDescriptor;

  public FunctionNode(YonaLanguage language, SourceSection sourceSection, String moduleFQN, String name, int cardinality, FrameDescriptor frameDescriptor, ExpressionNode expression) {
    this.moduleFQN = moduleFQN;
    this.name = name;
    this.cardinality = cardinality;
    this.expression = expression;
    this.language = language;
    this.sourceSection = sourceSection;
    this.frameDescriptor = frameDescriptor;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FunctionNode that = (FunctionNode) o;
    return Objects.equals(name, that.name) &&
        Objects.equals(cardinality, that.cardinality) &&
        Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, cardinality, expression);
  }

  @Override
  public String toString() {
    return "FunctionNode{" +
        "name='" + name + '\'' +
        ", cardinality=" + cardinality +
        ", expression=" + expression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.expression.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public String[] requiredIdentifiers() {
    return expression.getRequiredIdentifiers();
  }

  @Override
  public Function executeFunction(VirtualFrame frame) {
    return execute(frame);
  }

  private Function execute(VirtualFrame frame) {
    ClosureRootNode rootNode = new ClosureRootNode(language, frameDescriptor, expression, sourceSection, moduleFQN, name, frame.materialize());
    return new Function(moduleFQN, name, Truffle.getRuntime().createCallTarget(rootNode), cardinality, true);
  }

  @Override
  public String name() {
    return name;
  }
}
