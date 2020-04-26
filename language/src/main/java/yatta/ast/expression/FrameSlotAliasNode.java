package yatta.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.FrameSlotKind;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import yatta.ast.ExpressionNode;
import yatta.ast.local.WriteLocalVariableNode;
import yatta.ast.local.WriteLocalVariableNodeGen;

import java.util.Objects;

public final class FrameSlotAliasNode extends ExpressionNode implements AliasNode {
  public final FrameSlot frameSlot;
  @Node.Child
  public ExpressionNode expression;

  @CompilerDirectives.TruffleBoundary
  public FrameSlotAliasNode(FrameSlot frameSlot, ExpressionNode expression) {
    this.frameSlot = frameSlot;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FrameSlotAliasNode aliasNode = (FrameSlotAliasNode) o;
    return Objects.equals(frameSlot, aliasNode.frameSlot) &&
        Objects.equals(expression, aliasNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(frameSlot, expression);
  }

  @Override
  public String toString() {
    return "FrameSlotAliasNode{" +
        "frameSlot='" + frameSlot + '\'' +
        ", expression=" + expression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    CompilerDirectives.transferToInterpreterAndInvalidate();
    WriteLocalVariableNode writeLocalVariableNode = WriteLocalVariableNodeGen.create(expression, frameSlot);
    return writeLocalVariableNode.executeGeneric(frame);
  }
}
