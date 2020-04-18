package yatta.ast;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.*;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import com.oracle.truffle.api.source.SourceSection;
import yatta.YattaLanguage;

/**
 * The root of all YattaLanguage execution trees. It is a Truffle requirement that the tree root extends the
 * class {@link RootNode}. This class is used for both builtin and user-defined functions. For
 * builtin functions, the {@link #bodyNode} is a subclass of {@link yatta.ast.builtin.BuiltinNode}.
 */
@NodeInfo(language = "yatta", description = "The root of all yatta execution trees")
public class ClosureRootNode extends RootNode {
  /**
   * The function body that is executed, and specialized during execution.
   */
  @Child
  private ExpressionNode bodyNode;

  /**
   * The name of the function, for printing purposes only.
   */
  private String name;

  private String moduleFQN;

  private final SourceSection sourceSection;

  private final MaterializedFrame lexicalScope;

  public ClosureRootNode(YattaLanguage language, FrameDescriptor frameDescriptor, ExpressionNode bodyNode,
                         SourceSection sourceSection, String moduleFQN, String name, MaterializedFrame lexicalScope) {
    super(language, frameDescriptor);
    this.bodyNode = bodyNode;
    this.name = name;
    this.moduleFQN = moduleFQN;
    this.sourceSection = sourceSection;
    this.lexicalScope = lexicalScope;
  }

  @Override
  public SourceSection getSourceSection() {
    return sourceSection;
  }

  @Override
  public Object execute(VirtualFrame frame) {
    CompilerDirectives.transferToInterpreterAndInvalidate();
    for (Object identifier : lexicalScope.getFrameDescriptor().getIdentifiers()) {
      FrameSlot oldFrameSlot = lexicalScope.getFrameDescriptor().findFrameSlot(identifier);
      FrameSlot newFrameSlot = frame.getFrameDescriptor().findOrAddFrameSlot(identifier, FrameSlotKind.Illegal);
      frame.setObject(newFrameSlot, lexicalScope.getValue(oldFrameSlot));
    }

    return bodyNode.executeGeneric(frame);
  }

  @Override
  public String getName() {
    return name;
  }

  @Override
  public String getQualifiedName() {
    if (moduleFQN != null) {
      return moduleFQN + "::" + name;
    } else {
      return name;
    }
  }

  public void setName(String name) {
    if (this.name == null) {
      this.name = name;
    }
  }

  @Override
  public String toString() {
    return "closure:" + name;
  }
}
