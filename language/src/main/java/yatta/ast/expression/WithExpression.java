package yatta.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.value.ContextLookupNode;
import yatta.runtime.*;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;

@NodeInfo(shortName = "with")
public final class WithExpression extends ExpressionNode {
  private final String name;
  @Child
  public ExpressionNode contextExpression;
  @Child
  public ExpressionNode bodyExpression;
  @Child
  private InteropLibrary library;
  @Child
  public ExpressionNode resultNode;

  public WithExpression(String name, ExpressionNode contextExpression, ExpressionNode bodyExpression) {
    this.name = name;
    this.contextExpression = contextExpression;
    this.bodyExpression = bodyExpression;
    this.library = InteropLibrary.getFactory().createDispatched(3);

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

  private Object executeBodyWithIdentifier(final VirtualFrame frame, final String name, final Function enterFunction, final Function leaveFunction, final Object contextManagerData) {
    Context context = lookupContextReference(YattaLanguage.class).get();

    if (context.containsLocalContext(name)) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("Duplicate context identifier '" + name + "'", this);
    } else {
      return executeResultNode(frame, name, context, resultNode, enterFunction, leaveFunction, contextManagerData);
    }
  }

  private Object executeResultNode(final VirtualFrame frame, final String name, final Context context, final ExpressionNode resultNode, final Function enterFunction, final Function leaveFunction, final Object contextValue) {
    boolean shouldCleanup = true;
    try {
      // Execute enter function with whatever being returned from the "with (contextValue)" part. It's result is then stored as the local context and also passed to leave function.
      Object enterResult = library.execute(enterFunction, contextValue);
      context.putLocalContext(name, new ContextManager<>(name, enterFunction, leaveFunction, enterResult));
      // Execute the body.
      Object resultValue = resultNode.executeGeneric(frame);

      if (resultValue instanceof Promise) {
        shouldCleanup = false;
        Promise resultPromise = (Promise) resultValue;

        return resultPromise.map(value -> {
          try {
            // Execute leave function, with the result of enter function.
            library.execute(leaveFunction, new ContextManager<>(name, enterFunction, leaveFunction, enterResult));
            return value;
          } catch (UnsupportedTypeException | UnsupportedMessageException | ArityException e) {
            throw new YattaException(e, this);
          } finally {
            context.removeLocalContext(name);
          }
        }, exception -> {
          context.removeLocalContext(name);
          return exception;
        }, this);
      } else {
        return library.execute(leaveFunction, resultValue);
      }
    } catch (UnsupportedTypeException | UnsupportedMessageException | ArityException e) {
      throw new YattaException(e, this);
    } finally {
      if (shouldCleanup) {
        context.removeLocalContext(name);
      }
    }
  }

  private Object executeBodyWithoutIdentifierContext(final VirtualFrame frame, final Object contextValue) {
    if (contextValue instanceof ContextManager) {
      ContextManager<?> contextManager = (ContextManager<?>) contextValue;
      return executeBodyWithIdentifier(frame, name != null ? name : contextManager.contextIdentifier().asJavaString(this), contextManager.enterFunction(), contextManager.leaveFunction(), contextManager.data());
    } else if (contextValue instanceof Tuple) {
      Object contextValueObj = ContextManager.ensureValid((Tuple) contextValue, this);
      return executeBodyWithoutIdentifierContext(frame, contextValueObj);
    } else if (contextValue instanceof Promise) {
      Promise contextValuePromise = (Promise) contextValue;
      CompilerDirectives.transferToInterpreterAndInvalidate();
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
