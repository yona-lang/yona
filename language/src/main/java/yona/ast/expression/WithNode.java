package yona.ast.expression;

import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.ExpressionNode;
import yona.ast.call.InvokeNode;
import yona.runtime.ContextManager;
import yona.runtime.DependencyUtils;
import yona.runtime.async.Promise;

import java.util.Objects;

@NodeInfo(shortName = "withNode")
public final class WithNode extends LexicalScopeNode {
  @Child
  public ExtractContextNameNode extractContextNameNode;
  @Child
  public ExpressionNode expression;
  @Child
  private InteropLibrary library;

  public WithNode(ExtractContextNameNode extractContextNameNode, ExpressionNode expression) {
    this.extractContextNameNode = extractContextNameNode;
    this.expression = expression;
    this.library = InteropLibrary.getFactory().createDispatched(3);
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    WithNode that = (WithNode) o;
    return Objects.equals(extractContextNameNode, that.extractContextNameNode) && Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(extractContextNameNode, expression);
  }

  @Override
  public String toString() {
    return "WithNode{" +
        "alias=" + extractContextNameNode +
        ", expression=" + expression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.expression.setIsTail(isTail);
  }

  @Override
  @ExplodeLoop
  public Object executeGeneric(VirtualFrame frame) {
    final Object result = extractContextNameNode.executeGeneric(frame);

    if (result instanceof Promise promise) {
      MaterializedFrame materializedFrame = frame.materialize();
      return promise.map(contextManager -> executeWithContextManager(materializedFrame, contextManager), this);
    } else {
      return executeWithContextManager(frame, result);
    }
  }

  @SuppressWarnings("unchecked")
  public Object executeWithContextManager(VirtualFrame frame, Object contextManagerObj) {
    final ContextManager<Object> contextManager = (ContextManager<Object>) contextManagerObj;
    return InvokeNode.dispatchFunction(contextManager.wrapperFunction(), library, this, contextManager, expression.executeGeneric(frame));
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiersWith(expression, extractContextNameNode);
  }
}
