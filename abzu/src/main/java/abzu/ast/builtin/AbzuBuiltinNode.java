package abzu.ast.builtin;

import com.oracle.truffle.api.dsl.GenerateNodeFactory;
import com.oracle.truffle.api.dsl.NodeChild;
import com.oracle.truffle.api.dsl.NodeField;
import com.oracle.truffle.api.dsl.UnsupportedSpecializationException;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import abzu.AbzuException;
import abzu.AbzuLanguage;
import abzu.ast.AbzuExpressionNode;
import abzu.runtime.AbzuContext;

/**
 * Base class for all builtin functions. It contains the Truffle DSL annotation {@link NodeChild}
 * that defines the function arguments.<br>
 * Builtin functions need access to the {@link AbzuContext}. Instead of defining a Java field manually
 * and setting it in a constructor, we use the Truffle DSL annotation {@link NodeField} that
 * generates the field and constructor automatically.
 * <p>
 * The builtin functions are registered in {@link AbzuContext#installBuiltins}. Every builtin node
 * subclass is instantiated there, wrapped into a function, and added to the
 * {@link AbzuFunctionRegistry}. This ensures that builtin functions can be called like user-defined
 * functions; there is no special function lookup or call node for builtin functions.
 */
@NodeChild(value = "arguments", type = AbzuExpressionNode[].class)
@GenerateNodeFactory
public abstract class AbzuBuiltinNode extends AbzuExpressionNode {

  /**
   * Accessor for the {@link AbzuContext}. The implementation of this method is generated
   * automatically based on the {@link NodeField} annotation on the class.
   */
  public final AbzuContext getContext() {
    return getRootNode().getLanguage(AbzuLanguage.class).getContextReference().get();
  }

  @Override
  public final Object executeGeneric(VirtualFrame frame) {
    try {
      return execute(frame);
    } catch (UnsupportedSpecializationException e) {
      throw AbzuException.typeError(e.getNode(), e.getSuppliedValues());
    }
  }

  @Override
  public final boolean executeBoolean(VirtualFrame frame) throws UnexpectedResultException {
    return super.executeBoolean(frame);
  }

  @Override
  public final long executeLong(VirtualFrame frame) throws UnexpectedResultException {
    return super.executeLong(frame);
  }

  protected abstract Object execute(VirtualFrame frame);
}
