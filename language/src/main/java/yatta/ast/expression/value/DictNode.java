package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;
import yatta.runtime.Dict;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.runtime.Murmur3;

import java.util.Objects;

@NodeInfo
public final class DictNode extends ExpressionNode {
  @Children public final EntryNode[] items;

  public DictNode(EntryNode[] items) {
    this.items = items;
  }

  public static final class EntryNode extends ExpressionNode {
    @Child private ExpressionNode key;
    @Child private ExpressionNode value;

    public EntryNode(ExpressionNode key, ExpressionNode value) {
      this.key = key;
      this.value = value;
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) return true;
      if (o == null || getClass() != o.getClass()) return false;
      EntryNode entryNode = (EntryNode) o;
      return Objects.equals(key, entryNode.key) &&
          Objects.equals(value, entryNode.value);
    }

    @Override
    public int hashCode() {
      return Objects.hash(key, value);
    }

    @Override
    public String toString() {
      return "EntryNode{" +
          "key=" + key +
          ", value=" + value +
          '}';
    }

    @Override
    public Object executeGeneric(VirtualFrame frame) {
      return new Object[] {key.executeGeneric(frame), value.executeGeneric(frame)};
    }
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    DictNode dictNode = (DictNode) o;
    return Objects.equals(items, dictNode.items);
  }

  @Override
  public int hashCode() {
    return Objects.hash(items);
  }

  @Override
  public String toString() {
    return "DictNode{" +
        "items=" + items +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Dict executeDictionary(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Dict execute(VirtualFrame frame) {
    Dict dictionary = Dict.EMPTY;

    for (EntryNode entryNode : items) {
      Object[] executedEntry = (Object[]) entryNode.executeGeneric(frame);
      dictionary = dictionary.add(executedEntry[0], executedEntry[1]);
    }

    return dictionary;
  }
}
