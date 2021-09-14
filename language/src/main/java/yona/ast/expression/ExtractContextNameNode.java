package yona.ast.expression;

import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.FrameSlotKind;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.expression.value.AnyValueNode;
import yona.ast.local.WriteLocalVariableNode;
import yona.ast.local.WriteLocalVariableNodeGen;
import yona.runtime.ContextManager;
import yona.runtime.Function;
import yona.runtime.Seq;
import yona.runtime.Tuple;
import yona.runtime.async.Promise;

import java.util.Objects;

@NodeInfo(shortName = "extractContextName")
public class ExtractContextNameNode extends AliasNode {
  public final String name;
  @Node.Child
  public ExpressionNode contextExpression;

  public ExtractContextNameNode(String name, ExpressionNode contextExpression) {
    this.name = name;
    this.contextExpression = contextExpression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ExtractContextNameNode that = (ExtractContextNameNode) o;
    return Objects.equals(name, that.name) && Objects.equals(contextExpression, that.contextExpression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, contextExpression);
  }

  @Override
  public String toString() {
    return "ExtractContextNameNode{" +
        "name='" + name + '\'' +
        ", contextExpression=" + contextExpression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object contextValue = contextExpression.executeGeneric(frame);
    return executeBodyWithoutIdentifierContext(frame, contextValue);
  }

  private Object executeBodyWithoutIdentifierContext(final VirtualFrame frame, final Object contextValue) {
    if (contextValue instanceof ContextManager<?> contextManager) {
      Seq nameSeq = name != null ? Seq.fromCharSequence(name) : contextManager.contextIdentifier();
      String nameString = name != null ? name : contextManager.contextIdentifier().asJavaString(this);
      return executeBodyWithIdentifier(frame, nameSeq, nameString, contextManager.wrapperFunction(), contextManager.getData(Object.class, this));
    } else if (contextValue instanceof Tuple) {
      Object contextValueObj = ContextManager.ensureValid((Tuple) contextValue, this);
      return executeBodyWithoutIdentifierContext(frame, contextValueObj);
    } else if (contextValue instanceof Promise contextValuePromise) {
      MaterializedFrame materializedFrame = frame.materialize();
      return contextValuePromise.map((result) -> executeBodyWithoutIdentifierContext(materializedFrame, result), this);
    } else if (contextValue instanceof Object[]) {
      ContextManager<Object> contextManager = ContextManager.fromItems((Object[]) contextValue, this);
      return executeBodyWithoutIdentifierContext(frame, contextManager);
    } else {
      throw ContextManager.invalidContextException(contextValue, null, this);
    }
  }

  private Object executeBodyWithIdentifier(final VirtualFrame frame, final Seq nameSeq, final String nameString, final Function wrapFunction, final Object contextManagerData) {
    final ContextManager<Object> contextManager = new ContextManager<>(nameSeq, wrapFunction, contextManagerData);

    FrameSlot frameSlot = frame.getFrameDescriptor().findOrAddFrameSlot(nameString, FrameSlotKind.Object);
    WriteLocalVariableNode writeLocalVariableNode = WriteLocalVariableNodeGen.create(new AnyValueNode(contextManager), frameSlot);
    return writeLocalVariableNode.executeGeneric(frame);
  }

  @Override
  public String[] requiredIdentifiers() {
    return contextExpression.getRequiredIdentifiers();
  }

  @Override
  protected String[] providedIdentifiers() {
    return name != null ? new String[]{name} : new String[]{};
  }
}
