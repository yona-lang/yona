package yona.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.runtime.Seq;

import java.util.Objects;

import static yona.parser.ParserUtil.unescapeYonaString;

@NodeInfo(shortName = "string")
public final class StringNode extends LiteralValueNode {
  public final Seq value;

  public StringNode(Seq value) {
    this.value = value;
  }

  public StringNode(CharSequence value) {
    this.value = Seq.fromCharSequence(unescapeYonaString(value));
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    StringNode that = (StringNode) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "StringNode{" +
        "value='" + value + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return value;
  }

  @Override
  public String executeString(VirtualFrame frame) throws UnexpectedResultException {
    return value.asJavaString(this);
  }
}
