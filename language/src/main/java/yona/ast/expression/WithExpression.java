package yona.ast.expression;

import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.ast.ExpressionNode;
import yona.ast.call.InvokeNode;
import yona.ast.expression.value.ContextLookupNode;
import yona.ast.expression.value.FunctionNode;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;

@NodeInfo(shortName = "with")
public final class WithExpression extends ExpressionNode {
  private final String name;
  @Child
  public ExpressionNode contextExpression;
  @Child
  public FunctionNode bodyExpression;
  @Child
  private InteropLibrary library;
  @Child
  public ExpressionNode resultNode;

  private final boolean isDaemon;
  private final YonaLanguage language;

  public WithExpression(YonaLanguage language, String name, ExpressionNode contextExpression, FunctionNode bodyExpression, boolean isDaemon) {
    this.language = language;
    this.name = name;
    this.contextExpression = contextExpression;
    this.bodyExpression = bodyExpression;
    this.library = InteropLibrary.getFactory().createDispatched(3);
    this.isDaemon = isDaemon;

    if (this.name != null) {
      resultNode = new LetNode(new NameAliasNode[]{new NameAliasNode(name, new ContextLookupNode(name))}, bodyExpression);
    } else {
      resultNode = bodyExpression;
    }
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    bodyExpression.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object contextValue = contextExpression.executeGeneric(frame);
    return executeBodyWithoutIdentifierContext(frame, contextValue);
  }

  private Object executeBodyWithIdentifier(final VirtualFrame frame, final String name, final Function wrapFunction, final Object contextManagerData) {
    Context context = lookupContextReference(YonaLanguage.class).get();

    if (context.containsLocalContext(name)) {
      throw new BadArgException("Duplicate context identifier '" + name + "'", this);
    }

    final ContextManager<Object> contextManager = new ContextManager<>(name, wrapFunction, contextManagerData);
    context.putLocalContext(name, contextManager);
    Object finalResult;

    boolean contextRemovedInPromise = false;
    try {
      // Execute the body. The result should be a function, or a promise with a function. This function is then passed as an argument to the wrapping function from the ctx manager.
      Object resultValue = resultNode.executeGeneric(frame);

      if (resultValue instanceof Promise resultPromise) {
        contextRemovedInPromise = true;
        finalResult = resultPromise.map(value -> {
          boolean contextRemovedInPromise2 = false;
          try {
            final Object result = InvokeNode.dispatchFunction(wrapFunction, library, this, contextManager, value);

            if (result instanceof Promise promise) {
              contextRemovedInPromise2 = true;
              return promise.map(res -> {
                try {
                  return res;
                } finally {
                  context.removeLocalContext(name);
                }
              }, exception -> {
                try {
                  return exception;
                } finally {
                  context.removeLocalContext(name);
                }
              }, this);
            } else {
              return result;
            }
          } finally {
            if (!contextRemovedInPromise2) {
              context.removeLocalContext(name);
            }
          }
        }, exception -> {
          context.removeLocalContext(name);
          return exception;
        }, this);
      } else {
        finalResult = InvokeNode.dispatchFunction(wrapFunction, library, this, contextManager, resultValue);

        if (finalResult instanceof Promise promise) {
          contextRemovedInPromise = true;
          finalResult = promise.map(res -> {
            try {
              return res;
            } finally {
              context.removeLocalContext(name);
            }
          }, exception -> {
            try {
              return exception;
            } finally {
              context.removeLocalContext(name);
            }
          }, this);
        }
      }
    } finally {
      if (!contextRemovedInPromise) {
        context.removeLocalContext(name);
      }
    }

    return isDaemon ? Unit.INSTANCE : finalResult;
  }

  private Object executeBodyWithoutIdentifierContext(final VirtualFrame frame, final Object contextValue) {
    if (contextValue instanceof ContextManager<?> contextManager) {
      return executeBodyWithIdentifier(frame, name != null ? name : contextManager.contextIdentifier().asJavaString(this), contextManager.wrapperFunction(), contextManager.getData(Object.class, this));
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

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(contextExpression, bodyExpression);
  }
}
