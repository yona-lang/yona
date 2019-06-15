package yatta.ast;

import yatta.YattaLanguage;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.*;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import com.oracle.truffle.api.source.SourceSection;

/**
 * The root of all YattaLanguage execution trees. It is a Truffle requirement that the tree root extends the
 * class {@link RootNode}. This class is used for both builtin and user-defined functions. For
 * builtin functions, the {@link #bodyNode} is a subclass of {@link yatta.ast.builtin.BuiltinNode}.
 */
@NodeInfo(language = "yatta", description = "The root of all yatta execution trees")
public class YattaRootNode extends RootNode {
  /**
   * The function body that is executed, and specialized during execution.
   */
  @Child
  private ExpressionNode bodyNode;

  /**
   * The name of the function, for printing purposes only.
   */
  private String name;

  private final SourceSection sourceSection;

  private final MaterializedFrame lexicalScope;

  public YattaRootNode(YattaLanguage language, FrameDescriptor frameDescriptor, ExpressionNode bodyNode,
                       SourceSection sourceSection, String name, MaterializedFrame lexicalScope) {
    super(language, frameDescriptor);
    this.bodyNode = bodyNode;
    this.name = name;
    this.sourceSection = sourceSection;
    this.lexicalScope = lexicalScope;
  }

  @Override
  public SourceSection getSourceSection() {
    return sourceSection;
  }

  @Override
  public Object execute(VirtualFrame frame) {
    CompilerDirectives.transferToInterpreter();
    for (Object identifier : lexicalScope.getFrameDescriptor().getIdentifiers()) {
      FrameSlot oldFrameSlot = lexicalScope.getFrameDescriptor().findFrameSlot(identifier);
      FrameSlot newFrameSlot = frame.getFrameDescriptor().findOrAddFrameSlot(identifier, FrameSlotKind.Illegal);
      frame.setObject(newFrameSlot, lexicalScope.getValue(oldFrameSlot));
    }

    assert lookupContextReference(YattaLanguage.class).get() != null;
    return bodyNode.executeGeneric(frame);
  }

  @Override
  public String getName() {
    return name;
  }

  public void setName(String name) {
    if (this.name == null) {
      this.name = name;
    }
  }

  @Override
  public String toString() {
    return "root " + name;
  }
}
