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
  public final Entry[] items;

  public DictNode(Entry[] items) {
    this.items = items;
  }

  public static final class Entry {
    public final ExpressionNode key;
    public final ExpressionNode value;

    public Entry(ExpressionNode key, ExpressionNode value) {
      this.key = key;
      this.value = value;
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) return true;
      if (o == null || getClass() != o.getClass()) return false;
      Entry entry = (Entry) o;
      return Objects.equals(key, entry.key) &&
          Objects.equals(value, entry.value);
    }

    @Override
    public int hashCode() {
      return Objects.hash(key, value);
    }

    @Override
    public String toString() {
      return "Entry{" +
          "key=" + key +
          ", value=" + value +
          '}';
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
    Dict dictionary = Dict.empty(Murmur3.INSTANCE, 0L);

    for (Entry entry : items) {
      dictionary = dictionary.add(entry.key.executeGeneric(frame), entry.value.executeGeneric(frame));
    }

    return dictionary;
  }
}
