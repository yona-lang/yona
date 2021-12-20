package yona.ast.expression.value;

import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.SourceSection;
import yona.YonaLanguage;
import yona.ast.FunctionRootNode;
import yona.runtime.Function;

import java.util.Objects;

@NodeInfo
public final class LiteralFunctionNode extends FunctionLikeNode {
  private final String moduleFQN, name;
  @Node.Child
  public LiteralValueNode literalValueNode;

  private YonaLanguage language;
  private SourceSection sourceSection;
  private FrameDescriptor frameDescriptor;

  public LiteralFunctionNode(YonaLanguage language, SourceSection sourceSection, String moduleFQN, String name, FrameDescriptor frameDescriptor, LiteralValueNode literalValueNode) {
    this.moduleFQN = moduleFQN;
    this.name = name;
    this.literalValueNode = literalValueNode;
    this.language = language;
    this.sourceSection = sourceSection;
    this.frameDescriptor = frameDescriptor;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    LiteralFunctionNode that = (LiteralFunctionNode) o;
    return Objects.equals(literalValueNode, that.literalValueNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(literalValueNode);
  }

  @Override
  public String toString() {
    return "FunctionLiteralNode{" +
           "literalValueNode=" + literalValueNode +
           '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.literalValueNode.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    FunctionRootNode rootNode = new FunctionRootNode(language, frameDescriptor, literalValueNode, sourceSection, moduleFQN, name);
    return new Function(moduleFQN, name, Truffle.getRuntime().createCallTarget(rootNode), 0, true);
  }

  @Override
  protected String[] requiredIdentifiers() {
    return literalValueNode.requiredIdentifiers();
  }

  @Override
  public String name() {
    return name;
  }
}
