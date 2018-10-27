package abzu.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.AbzuExpressionNode;
import abzu.ast.expression.ValueNode;

import java.util.List;
import java.util.Objects;

@NodeInfo
public final class DictNode extends ValueNode<List<DictNode.Entry>> {
  public final List<Entry> items;

  public DictNode(List<Entry> items) {
    this.items = items;
  }

  @Override
  public List<Entry> executeValue(VirtualFrame frame) {
    return items;
  }

  public static final class Entry {
    public final String key;
    public final AbzuExpressionNode value;

    public Entry(String key, AbzuExpressionNode value) {
      this.key = key;
      this.value = value;
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
}
