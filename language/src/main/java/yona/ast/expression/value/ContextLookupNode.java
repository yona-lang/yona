package yona.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.ExpressionNode;
import yona.runtime.Context;
import yona.runtime.Unit;

import java.util.Objects;

@NodeInfo(shortName = "context-lookup")
public final class ContextLookupNode extends ExpressionNode {
  public final String name;

  public ContextLookupNode(String name) {
    this.name = name;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ContextLookupNode that = (ContextLookupNode) o;
    return Objects.equals(name, that.name);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name);
  }

  @Override
  public String toString() {
    return "ContextLookupNode{" +
        "name=" + name +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Context context = lookupContextReference(YonaLanguage.class).get();
    Object result = context.lookupLocalContext(name);
    if (result == Unit.INSTANCE) {
      throw new YonaException("Context identifier '" + name + "' not found in the current scope", this);
    } else {
      return result;
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }
}
