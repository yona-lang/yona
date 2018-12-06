package abzu.ast;

import abzu.AbzuLanguage;
import abzu.ast.local.WriteLocalVariableNode;
import abzu.ast.local.WriteLocalVariableNodeGen;
import com.oracle.truffle.api.CompilerDirectives.CompilationFinal;
import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.TruffleRuntime;
import com.oracle.truffle.api.frame.*;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import com.oracle.truffle.api.source.SourceSection;

/**
 * The root of all AbzuLanguage execution trees. It is a Truffle requirement that the tree root extends the
 * class {@link RootNode}. This class is used for both builtin and user-defined functions. For
 * builtin functions, the {@link #bodyNode} is a subclass of {@link abzu.ast.builtin.BuiltinNode}.
 */
@NodeInfo(language = "abzu", description = "The root of all abzu execution trees")
public class AbzuRootNode extends RootNode {
  /**
   * The function body that is executed, and specialized during execution.
   */
  @Child
  private ExpressionNode bodyNode;

  /**
   * The name of the function, for printing purposes only.
   */
  private String name;

  @CompilationFinal
  private boolean isCloningAllowed;

  private final SourceSection sourceSection;

  private final MaterializedFrame lexicalScope;

  public AbzuRootNode(AbzuLanguage language, FrameDescriptor frameDescriptor, ExpressionNode bodyNode,
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
    for (Object identifier : lexicalScope.getFrameDescriptor().getIdentifiers()) {
      FrameSlot oldFrameSlot = lexicalScope.getFrameDescriptor().findFrameSlot(identifier);
      FrameSlot newFrameSlot = frame.getFrameDescriptor().findOrAddFrameSlot(identifier, FrameSlotKind.Illegal);
      frame.setObject(newFrameSlot, lexicalScope.getValue(oldFrameSlot));
    }

    assert getLanguage(AbzuLanguage.class).getContextReference().get() != null;
    return bodyNode.executeGeneric(frame);
  }

  public ExpressionNode getBodyNode() {
    return bodyNode;
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

  public void setCloningAllowed(boolean isCloningAllowed) {
    this.isCloningAllowed = isCloningAllowed;
  }

  @Override
  public boolean isCloningAllowed() {
    return isCloningAllowed;
  }

  @Override
  public String toString() {
    return "root " + name;
  }
}
