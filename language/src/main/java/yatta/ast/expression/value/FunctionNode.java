package yatta.ast.expression.value;

import yatta.YattaLanguage;
import yatta.ast.YattaRootNode;
import yatta.ast.ExpressionNode;
import yatta.runtime.Function;
import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.source.SourceSection;

import java.util.Objects;

@NodeInfo
public final class FunctionNode extends ExpressionNode {
  public final String name;
  private int cardinality;
  @Node.Child
  public ExpressionNode expression;

  private YattaLanguage language;
  private SourceSection sourceSection;
  private FrameDescriptor frameDescriptor;

  public FunctionNode(YattaLanguage language, SourceSection sourceSection, String name, int cardinality, FrameDescriptor frameDescriptor, ExpressionNode expression) {
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
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Function executeFunction(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Function execute(VirtualFrame frame) {
    YattaRootNode rootNode = new YattaRootNode(language, frameDescriptor, expression, sourceSection, name, frame.materialize());
    return new Function(name, Truffle.getRuntime().createCallTarget(rootNode), cardinality);
  }
}
