package yona.ast;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.*;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import com.oracle.truffle.api.source.SourceSection;
import yona.YonaLanguage;

/**
 * The root of all YonaLanguage execution trees. It is a Truffle requirement that the tree root extends the
 * class {@link RootNode}. This class is used for both builtin and user-defined functions. For
 * builtin functions, the {@link #bodyNode} is a subclass of {@link yona.ast.builtin.BuiltinNode}.
 */
@NodeInfo(language = "yona", description = "The root of all yona execution trees")
public class ClosureRootNode extends YonaRootNode {
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

  public ClosureRootNode(YonaLanguage language, FrameDescriptor frameDescriptor, ExpressionNode bodyNode,
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
    final FrameDescriptor fd = lexicalScope.getFrameDescriptor();
    CompilerDirectives.transferToInterpreterAndInvalidate();
    for (Object identifier : fd.getIdentifiers()) {
      FrameSlot oldFrameSlot = fd.findFrameSlot(identifier);
      FrameSlotKind kind = fd.getFrameSlotKind(oldFrameSlot);
      FrameSlot newFrameSlot = frame.getFrameDescriptor().findOrAddFrameSlot(identifier, kind);

      try {
        switch (kind) {
          case Long -> frame.setLong(newFrameSlot, lexicalScope.getLong(oldFrameSlot));
          case Int -> frame.setInt(newFrameSlot, lexicalScope.getInt(oldFrameSlot));
          case Double -> frame.setDouble(newFrameSlot, lexicalScope.getDouble(oldFrameSlot));
          case Float -> frame.setFloat(newFrameSlot, lexicalScope.getFloat(oldFrameSlot));
          case Boolean -> frame.setBoolean(newFrameSlot, lexicalScope.getBoolean(oldFrameSlot));
          case Byte -> frame.setByte(newFrameSlot, lexicalScope.getByte(oldFrameSlot));
          case Object -> frame.setObject(newFrameSlot, lexicalScope.getObject(oldFrameSlot));
          case Illegal -> frame.setObject(newFrameSlot, lexicalScope.getValue(oldFrameSlot));
        }
      } catch (FrameSlotTypeException e) {
        frame.setObject(newFrameSlot, lexicalScope.getValue(oldFrameSlot));
      }
    }

    return bodyNode.executeGeneric(frame);
  }

  @Override
  public String getName() {
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
