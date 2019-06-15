package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;
import yatta.runtime.StringList;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class NonEmptyStringListNode extends ExpressionNode {
  public final String[] strings;

  public NonEmptyStringListNode(String[] strings) {
    this.strings = strings;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    NonEmptyStringListNode listNode = (NonEmptyStringListNode) o;
    return Objects.equals(strings, listNode.strings);
  }

  @Override
  public int hashCode() {
    return Objects.hash(strings);
  }

  @Override
  public String toString() {
    return "NonEmptyStringListNode{" +
           "strings=" + Arrays.toString(strings) +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return Arrays.asList(strings);
  }

  @Override
  public StringList executeStringList(VirtualFrame frame) throws UnexpectedResultException {
    return new StringList(strings);
  }
}
