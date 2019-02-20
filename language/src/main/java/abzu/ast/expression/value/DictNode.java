package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import abzu.runtime.Dictionary;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Objects;

@NodeInfo
public final class DictNode extends ExpressionNode {
  public final Entry[] items;

  public DictNode(Entry[] items) {
    this.items = items;
  }

  public static final class Entry {
    public final Object key;
    public final ExpressionNode value;

    public Entry(Object key, ExpressionNode value) {
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
  public Dictionary executeDictionary(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Dictionary execute(VirtualFrame frame) {
    Dictionary dictionary = Dictionary.dictionary();

    for (Entry entry : items) {
      dictionary = dictionary.insert(entry.key, entry.value.executeGeneric(frame));
    }

    return dictionary;
  }
}
