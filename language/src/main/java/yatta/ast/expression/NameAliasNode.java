package yatta.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import yatta.ast.ExpressionNode;
import yatta.ast.local.WriteLocalVariableNode;
import yatta.ast.local.WriteLocalVariableNodeGen;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.FrameSlotKind;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.Objects;

public final class NameAliasNode extends ExpressionNode implements AliasNode {
  public final String name;
  @Node.Child
  public ExpressionNode expression;

  @CompilerDirectives.TruffleBoundary
  public NameAliasNode(String name, ExpressionNode expression) {
    this.name = name;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    NameAliasNode nameAliasNode = (NameAliasNode) o;
    return Objects.equals(name, nameAliasNode.name) &&
        Objects.equals(expression, nameAliasNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, expression);
  }

  @Override
  public String toString() {
    return "NameAliasNode{" +
        "name='" + name + '\'' +
        ", expression=" + expression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    CompilerDirectives.transferToInterpreterAndInvalidate();
    FrameSlot frameSlot = frame.getFrameDescriptor().findOrAddFrameSlot(name, FrameSlotKind.Illegal);
    WriteLocalVariableNode writeLocalVariableNode = WriteLocalVariableNodeGen.create(expression, frameSlot);
    return writeLocalVariableNode.executeGeneric(frame);
  }
}
