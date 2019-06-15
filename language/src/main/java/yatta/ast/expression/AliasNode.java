package yatta.ast.expression;

import yatta.ast.ExpressionNode;
import yatta.ast.local.WriteLocalVariableNode;
import yatta.ast.local.WriteLocalVariableNodeGen;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.FrameSlotKind;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.Objects;

public final class AliasNode extends ExpressionNode {
  public final String name;
  @Node.Child
  public ExpressionNode expression;

  public AliasNode(String name, ExpressionNode expression) {
    this.name = name;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AliasNode aliasNode = (AliasNode) o;
    return Objects.equals(name, aliasNode.name) &&
        Objects.equals(expression, aliasNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, expression);
  }

  @Override
  public String toString() {
    return "AliasNode{" +
        "name='" + name + '\'' +
        ", expression=" + expression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    FrameSlot frameSlot = frame.getFrameDescriptor().findOrAddFrameSlot(name, FrameSlotKind.Illegal);
    WriteLocalVariableNode writeLocalVariableNode = WriteLocalVariableNodeGen.create(expression, frameSlot);
    return writeLocalVariableNode.executeGeneric(frame);
  }
}
