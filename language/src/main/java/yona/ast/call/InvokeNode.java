package yona.ast.call;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.instrumentation.Tag;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.ClosureRootNode;
import yona.ast.ExpressionNode;
import yona.ast.controlflow.YonaBlockNode;
import yona.ast.expression.IdentifierNode;
import yona.ast.expression.value.AnyValueNode;
import yona.ast.local.ReadArgumentNode;
import yona.ast.local.WriteLocalVariableNode;
import yona.ast.local.WriteLocalVariableNodeGen;
import yona.runtime.DependencyUtils;
import yona.runtime.Function;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.exceptions.UndefinedNameException;

import java.util.Arrays;

/**
 * The node for function invocation in Yona. Since Yona has first class functions, the {@link yona.runtime.Function
 * target function} can be computed by an arbitrary expression. This node is responsible for
 * evaluating this expression, as well as evaluating the {@link #argumentNodes arguments}. The
 * actual dispatch is then delegated to a chain of {@link DispatchNode} that form a polymorphic
 * inline cache.
 */
@NodeInfo(shortName = "invoke")
public final class InvokeNode extends ExpressionNode {
  @Node.Child
  private ExpressionNode functionNode;
  private final Function function;
  @Node.Children
  private final ExpressionNode[] argumentNodes;
  @Child
  private InteropLibrary library;
  @Children
  private ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode | Because this is created from Stack.toArray, the last pushed element is the last element of the array

  private final YonaLanguage language;

  public InvokeNode(YonaLanguage language, ExpressionNode functionNode, ExpressionNode[] argumentNodes, ExpressionNode[] moduleStack) {
    assert functionNode != null;
    this.functionNode = functionNode;
    this.function = null;
    this.argumentNodes = argumentNodes;
    this.library = InteropLibrary.getFactory().createDispatched(3);
    this.language = language;
    this.moduleStack = moduleStack;
  }

  public InvokeNode(YonaLanguage language, Function function, ExpressionNode[] argumentNodes, ExpressionNode[] moduleStack) {
    assert function != null;
    this.functionNode = null;
    this.function = function;
    this.argumentNodes = argumentNodes;
    this.library = InteropLibrary.getFactory().createDispatched(3);
    this.language = language;
    this.moduleStack = moduleStack;
  }

  @Override
  public String toString() {
    return "InvokeNode{" +
        "functionNode=" + functionNode +
        ", function=" + function +
        ", argumentNodes=" + Arrays.toString(argumentNodes) +
        '}';
  }

  @ExplodeLoop
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    if (this.function != null) {
      return execute(this.function, frame);
    } else {
      Object maybeFunction = functionNode.executeGeneric(frame);
      if (maybeFunction instanceof Function) {
        return execute((Function) maybeFunction, frame);
      } else if (maybeFunction instanceof Promise promise) {
        return promise.map(value -> {
          if (value instanceof Function) {
            return execute((Function) value, frame);
          } else {
            throw notAFucntion(value);
          }
        }, this);
      } else {
        throw notAFucntion(maybeFunction);
      }
    }
  }

  private RuntimeException notAFucntion(Object value) {
    return new YonaException("Cannot invoke non-function value: %s".formatted(value), this);
  }

  private Object execute(Function function, VirtualFrame frame) {
    /*
     * The number of arguments is constant for one invoke node. During compilation, the loop is
     * unrolled and the execute methods of all arguments are inlined. This is triggered by the
     * ExplodeLoop annotation on the method. The compiler assertion below illustrates that the
     * array length is really constant.
     */
    CompilerAsserts.compilationConstant(argumentNodes.length);
    CompilerAsserts.compilationConstant(function.getCardinality());
    CompilerAsserts.compilationConstant(this.isTail());

    if (argumentNodes.length > function.getCardinality()) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("Unexpected number of arguments when calling '%s': %d expected: %d".formatted(function.getName(), argumentNodes.length, function.getCardinality()), this);
    } else if (argumentNodes.length == 0 && function.getCardinality() > 0) {
      return function;
    } else if (argumentNodes.length < function.getCardinality()) {
      return createPartiallyAppliedClosure(function, frame);
    } else {
      Object[] argumentValues = new Object[argumentNodes.length];
      boolean unwrapPromises = checkArgsForPromises(frame, argumentValues, function.isUnwrapArgumentPromises());
      return dispatchFunction(function, function.isUnwrapArgumentPromises() && unwrapPromises, library, this, (Object[]) argumentValues);
    }
  }

  @ExplodeLoop
  private boolean checkArgsForPromises(VirtualFrame frame, Object[] argumentValues, boolean isUnwrapArgumentPromises) {
    boolean argsArePromise = false;
    for (int i = 0; i < argumentNodes.length; i++) {
      Object argValue = argumentNodes[i].executeGeneric(frame);
      if (argValue instanceof Promise) {
        argsArePromise = true;
        argumentValues[i] = argValue;
      } else if (argValue instanceof Function && !isUnwrapArgumentPromises) {
        argsArePromise = true;
        Promise promise = new Promise(library);
        promise.map((result) -> argValue, this);
        argumentValues[i] = promise;
      } else {
        argumentValues[i] = argValue;
      }
    }
    return argsArePromise;
  }

  public static Object dispatchFunction(Function function, boolean unwrapPromises, InteropLibrary library, ExpressionNode node, Object... argumentValues) {
    if (unwrapPromises) {
      Promise argsPromise = Promise.all(argumentValues, node);
      return argsPromise.map(argValues -> {
        try {
          return library.execute(function, (Object[]) argValues);
        } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
          /* Execute was not successful. */
          return UndefinedNameException.undefinedFunction(node, function);
        }
      }, node);
    } else {
      if (node.isTail()) {
        throw new TailCallException(function, argumentValues);
      }

      return dispatchFunction(function, library, node, (Object[]) argumentValues);
    }
  }

  public static Object dispatchFunction(Function function, InteropLibrary library, ExpressionNode node, Object... argumentValues) {
    Function dispatchFunction = function;
    while (true) {
      try {
        return library.execute(dispatchFunction, argumentValues);
      } catch (TailCallException e) {
        dispatchFunction = e.function;
        argumentValues = e.arguments;
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw UndefinedNameException.undefinedFunction(node, dispatchFunction);
      }
    }
  }

  private Object createPartiallyAppliedClosure(Function function, VirtualFrame frame) {
    /*
     * Create a closure for `partial`ly applied function
     */
    String partiallyAppliedFunctionName = "$partial-%d/%d-%s".formatted(argumentNodes.length, function.getCardinality(), function.getName());
    ExpressionNode[] allArgumentNodes = new ExpressionNode[function.getCardinality()];

    setEvaluatedArgs(frame, allArgumentNodes);
    setNotEvaluatedArgs(function, allArgumentNodes);

    /*
     * Partially applied function will just invoke the original function with arguments constructed as a combination
     * of those which were provided when this closure was created and those to be read on the following application
     */
    InvokeNode invokeNode = new InvokeNode(language, new IdentifierNode(language, function.getName(), moduleStack), allArgumentNodes, moduleStack);

    /*
     * We need to make sure that the original function is still accessible within the closure, even if the partially
     * applied function already leaves the scope with the original function
     */
    WriteLocalVariableNode writeLocalVariableNode;
    if (functionNode != null) {
      writeLocalVariableNode = WriteLocalVariableNodeGen.create(functionNode, frame.getFrameDescriptor().findOrAddFrameSlot(function.getName()));
    } else {
      writeLocalVariableNode = WriteLocalVariableNodeGen.create(new AnyValueNode(function), frame.getFrameDescriptor().findOrAddFrameSlot(function.getName()));
    }

    YonaBlockNode blockNode = new YonaBlockNode(new ExpressionNode[]{writeLocalVariableNode, invokeNode});
    ClosureRootNode rootNode = new ClosureRootNode(language, frame.getFrameDescriptor(), blockNode, getSourceSection(), function.getModuleFQN(), partiallyAppliedFunctionName, frame.materialize());
    return new Function(function.getModuleFQN(), partiallyAppliedFunctionName, Truffle.getRuntime().createCallTarget(rootNode), function.getCardinality() - argumentNodes.length, function.isUnwrapArgumentPromises());
  }

  /*
   * These are the new arguments, to be read on the actual application of this new closure
   */
  @ExplodeLoop
  private void setNotEvaluatedArgs(Function function, ExpressionNode[] allArgumentNodes) {
    CompilerAsserts.compilationConstant(argumentNodes.length);
    for (int i = argumentNodes.length, j = 0; i < function.getCardinality(); i++, j++) {
      allArgumentNodes[i] = new ReadArgumentNode(j);
    }
  }

  /*
   * These arguments are already on the stack, so they are evaluated and stored for later
   */
  @ExplodeLoop
  private void setEvaluatedArgs(VirtualFrame frame, ExpressionNode[] allArgumentNodes) {
    CompilerAsserts.compilationConstant(argumentNodes.length);
    for (int i = 0; i < argumentNodes.length; i++) {
      allArgumentNodes[i] = new AnyValueNode(argumentNodes[i].executeGeneric(frame));
    }
  }

  @Override
  public boolean hasTag(Class<? extends Tag> tag) {
    if (tag == StandardTags.CallTag.class) {
      return true;
    }
    return super.hasTag(tag);
  }

  @Override
  public String[] requiredIdentifiers() {
    if (function != null) {
      return DependencyUtils.catenateRequiredIdentifiers(argumentNodes);
    } else {
      return DependencyUtils.catenateRequiredIdentifiersWith(functionNode, argumentNodes);
    }
  }
}
