package yatta.ast.expression.value;

import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.source.SourceSection;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.FunctionRootNode;
import yatta.runtime.Function;

import java.util.Objects;

/**
 * Function defined in a module
 */
@NodeInfo
public final class ModuleFunctionNode extends FunctionLikeNode {
  private final String name;
  private final String moduleFQN;
  private int cardinality;
  @Child
  public ExpressionNode expression;

  private YattaLanguage language;
  private SourceSection sourceSection;
  private FrameDescriptor frameDescriptor;

  public ModuleFunctionNode(YattaLanguage language, SourceSection sourceSection, String moduleFQN, String name, int cardinality, FrameDescriptor frameDescriptor, ExpressionNode expression) {
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
    ModuleFunctionNode that = (ModuleFunctionNode) o;
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
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Function executeFunction(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Function execute(VirtualFrame frame) {
    FunctionRootNode rootNode = new FunctionRootNode(language, frameDescriptor, expression, sourceSection, moduleFQN, name);
    return new Function(moduleFQN, name, Truffle.getRuntime().createCallTarget(rootNode), cardinality);
  }

  @Override
  public String name() {
    return name;
  }
}
