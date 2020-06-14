package yatta.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.TypesGen;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.value.AnyValueNode;
import yatta.ast.expression.value.ContextLookupNode;
import yatta.runtime.Context;
import yatta.runtime.DependencyUtils;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;

@NodeInfo(shortName = "with")
public final class WithExpression extends ExpressionNode {
  private final String name;
  @Child
  public ExpressionNode contextExpression;
  @Child
  public ExpressionNode bodyExpression;

  public WithExpression(String name, ExpressionNode contextExpression, ExpressionNode bodyExpression) {
    this.name = name;
    this.contextExpression = contextExpression;
    this.bodyExpression = bodyExpression;
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    bodyExpression.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object contextValue = contextExpression.executeGeneric(frame);
    if (name != null) {
      return executeBodyWithIdentifier(frame, name, contextExpression);
    } else {
      return executeBodyWithoutIdentifierContext(frame, contextValue);
    }
  }

  private Object executeBodyWithIdentifier(VirtualFrame frame, String name, ExpressionNode contextExpression) {
    Context context = lookupContextReference(YattaLanguage.class).get();

    if (context.containsLocalContext(name)) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("Duplicate context identifier '" + name + "'", this);
    } else {
      ExpressionNode resultNode;
      if (this.name != null) {
        resultNode = new LetNode(new NameAliasNode[]{new NameAliasNode(name, new ContextLookupNode(name))}, bodyExpression);
      } else {
        resultNode = bodyExpression;
      }

      Object contextValue = contextExpression.executeGeneric(frame);
      if (contextValue instanceof Promise) {
        Promise contextPromise = (Promise) contextValue;
        CompilerDirectives.transferToInterpreterAndInvalidate();
        MaterializedFrame materializedFrame = frame.materialize();

        return contextPromise.map(value -> {
          try {
            context.putLocalContext(name, value);
            return resultNode.executeGeneric(materializedFrame);
          } finally {
            context.removeLocalContext(name);
          }
        }, exception -> {
          context.removeLocalContext(name);
          return exception;
        }, this);
      } else {
        try {
          context.putLocalContext(name, contextValue);
          return resultNode.executeGeneric(frame);
        } finally {
          context.removeLocalContext(name);
        }
      }
    }
  }

  private Object executeBodyWithoutIdentifierContext(VirtualFrame frame, Object contextValue) {
    if (contextValue instanceof Tuple) {
      Tuple contextValueTuple = (Tuple) contextValue;
      if (contextValueTuple.length() != 2) {
        throw new BadArgException("Context variable must either contain an identifier using 'as' syntax, or return a tuple of 2 elements, where the first element is a string", this);
      }

      Object evaluatedTuple = contextValueTuple.unwrapPromises(this);
      if (evaluatedTuple instanceof Promise) {
        Promise evaluatedTuplePromise = (Promise) evaluatedTuple;
        CompilerDirectives.transferToInterpreterAndInvalidate();
        MaterializedFrame materializedFrame = frame.materialize();
        return evaluatedTuplePromise.map((items) -> executeBodyWithoutIdentifierContext(materializedFrame, items), this);
      } else {
        return executeBodyWithoutIdentifierContext(frame, (Object[]) evaluatedTuple);
      }
    } else if (contextValue instanceof Promise) {
      Promise contextValuePromise = (Promise) contextValue;
      CompilerDirectives.transferToInterpreterAndInvalidate();
      MaterializedFrame materializedFrame = frame.materialize();
      return contextValuePromise.map((result) -> executeBodyWithoutIdentifierContext(materializedFrame, result), this);
    } else if (contextValue instanceof Object[]) {
      Object[] contextIdentifierItems = (Object[]) contextValue;
      if (contextIdentifierItems.length != 2) {
        throw new BadArgException("Context variable must either contain an identifier using 'as' syntax, or return a tuple of 2 elements, where the first element is a string", this);
      }
      try {
        return executeBodyWithIdentifier(frame, TypesGen.expectSeq(contextIdentifierItems[0]).asJavaString(this), new AnyValueNode(contextIdentifierItems[1]));
      } catch (UnexpectedResultException e) {
        throw new BadArgException(e, this);
      }
    } else {
      throw new BadArgException("Context variable must either contain an identifier using 'as' syntax, or return a tuple of 2 elements, where the first element is a string", this);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(contextExpression, bodyExpression);
  }
}
