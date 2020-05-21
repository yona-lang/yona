package yatta.ast.call;

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
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ClosureRootNode;
import yatta.ast.ExpressionNode;
import yatta.ast.controlflow.YattaBlockNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.AnyValueNode;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.local.WriteLocalVariableNode;
import yatta.ast.local.WriteLocalVariableNodeGen;
import yatta.runtime.Function;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.UndefinedNameException;

import java.util.Arrays;

/**
 * The node for function invocation in Yatta. Since Yatta has first class functions, the {@link yatta.runtime.Function
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

  private YattaLanguage language;

  public InvokeNode(YattaLanguage language, ExpressionNode functionNode, ExpressionNode[] argumentNodes, ExpressionNode[] moduleStack) {
    assert functionNode != null;
    this.functionNode = functionNode;
    this.function = null;
    this.argumentNodes = argumentNodes;
    this.library = InteropLibrary.getFactory().createDispatched(3);
    this.language = language;
    this.moduleStack = moduleStack;
  }

  public InvokeNode(YattaLanguage language, Function function, ExpressionNode[] argumentNodes, ExpressionNode[] moduleStack) {
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
      } else if (maybeFunction instanceof Promise) {
        Promise promise = (Promise) maybeFunction;
        return promise.map(value -> {
          if (value instanceof Function) {
            return execute((Function) value, frame);
          } else {
            throw notAFucntion(functionNode);
          }
        }, this);
      } else {
        throw notAFucntion(functionNode);
      }
    }
  }

  private RuntimeException notAFucntion(ExpressionNode functionNode) {
    CompilerDirectives.transferToInterpreterAndInvalidate();
    return new YattaException("Cannot invoke non-function node: " + functionNode, this);
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
      throw new YattaException("Unexpected number of arguments when calling '" + function.getName() +
          "': " + argumentNodes.length + " expected: " + function.getCardinality(), this);
    } else if (argumentNodes.length == 0 && function.getCardinality() > 0) {
      return function;
    } else if (argumentNodes.length < function.getCardinality()) {
      return createPartiallyAppliedClosure(function, frame);
    } else {
      Object[] argumentValues = new Object[argumentNodes.length];
      boolean unwrapPromises = checkArgsForPromises(frame, argumentValues);
      return dispatchFunction(function, argumentValues, function.isUnwrapArgumentPromises() && unwrapPromises);
    }
  }

  @ExplodeLoop
  private boolean checkArgsForPromises(VirtualFrame frame, Object[] argumentValues) {
    boolean argsArePromise = false;
    for (int i = 0; i < argumentNodes.length; i++) {
      Object argValue = argumentNodes[i].executeGeneric(frame);
      if (argValue instanceof Promise) {
        argsArePromise = true;
      }
      argumentValues[i] = argValue;
    }
    return argsArePromise;
  }

  private Object dispatchFunction(Function function, Object[] argumentValues, boolean unwrapPromises) {
    if (unwrapPromises) {
      Promise argsPromise = Promise.all(argumentValues, this);
      return argsPromise.map(argValues -> {
        try {
          return library.execute(function, (Object[]) argValues);
        } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
          /* Execute was not successful. */
          return UndefinedNameException.undefinedFunction(this, function);
        }
      }, this);
    } else {
      if (this.isTail()) {
        throw new TailCallException(function, argumentValues);
      }

      Function dispatchFunction = function;
      while (true) {
        try {
          return library.execute(dispatchFunction, argumentValues);
        } catch (TailCallException e) {
          dispatchFunction = e.function;
          argumentValues = e.arguments;
        } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
          /* Execute was not successful. */
          throw UndefinedNameException.undefinedFunction(this, dispatchFunction);
        }
      }
    }
  }

  private Object createPartiallyAppliedClosure(Function function, VirtualFrame frame) {
    CompilerDirectives.transferToInterpreterAndInvalidate();
    /*
     * Create a closure for `partial`ly applied function
     */
    String partiallyAppliedFunctionName = "$partial-" + argumentNodes.length + "/" + function.getCardinality() + "-" + function.getName();
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

    YattaBlockNode blockNode = new YattaBlockNode(new ExpressionNode[]{writeLocalVariableNode, invokeNode});
    ClosureRootNode rootNode = new ClosureRootNode(language, frame.getFrameDescriptor(), blockNode, getSourceSection(), function.getModuleFQN(), partiallyAppliedFunctionName, frame.materialize());
    return new Function(function.getModuleFQN(), partiallyAppliedFunctionName, Truffle.getRuntime().createCallTarget(rootNode), function.getCardinality() - argumentNodes.length, function.isUnwrapArgumentPromises());
  }

  /*
   * These are the new arguments, to be read on the actual application of this new closure
   */
  @ExplodeLoop
  private void setNotEvaluatedArgs(Function function, ExpressionNode[] allArgumentNodes) {
    for (int i = argumentNodes.length, j = 0; i < function.getCardinality(); i++, j++) {
      allArgumentNodes[i] = new ReadArgumentNode(j);
    }
  }

  /*
   * These arguments are already on the stack, so they are evaluated and stored for later
   */
  @ExplodeLoop
  private void setEvaluatedArgs(VirtualFrame frame, ExpressionNode[] allArgumentNodes) {
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
}
