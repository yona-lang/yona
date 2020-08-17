package yona.ast.expression.value;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Objects;

import static com.oracle.truffle.api.CompilerDirectives.CompilationFinal;

@NodeInfo
public class AnyValueNode extends LiteralValueNode {
  @CompilationFinal
  public final Object value;

  @CompilerDirectives.TruffleBoundary
  public AnyValueNode(Object value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AnyValueNode that = (AnyValueNode) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "AnyValueNode{" +
        "value=" + value +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    CompilerAsserts.compilationConstant(value);
    return value;
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }
}
