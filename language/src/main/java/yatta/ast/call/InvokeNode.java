package yatta.ast.call;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
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
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.controlflow.BlockNode;
import yatta.ast.expression.SimpleIdentifierNode;
import yatta.ast.expression.value.AnyValueNode;
import yatta.ast.expression.value.FunctionNode;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.local.WriteLocalVariableNodeGen;
import yatta.runtime.Function;
import yatta.runtime.UndefinedNameException;
import yatta.runtime.async.Promise;

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
  @Child private InteropLibrary library;

  private YattaLanguage language;

  public InvokeNode(YattaLanguage language, ExpressionNode functionNode, ExpressionNode[] argumentNodes) {
    this.functionNode = functionNode;
    this.function = null;
    this.argumentNodes = argumentNodes;
    this.library = InteropLibrary.getFactory().createDispatched(3);
    this.language = language;
  }

  public InvokeNode(YattaLanguage language, Function function, ExpressionNode[] argumentNodes) {
    this.functionNode = null;
    this.function = function;
    this.argumentNodes = argumentNodes;
    this.library = InteropLibrary.getFactory().createDispatched(3);
    this.language = language;
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
    final Function function;
    if (this.function != null) {
      function = this.function;
    } else {
      try {
        function = functionNode.executeFunction(frame);
      } catch (UnexpectedResultException e) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new YattaException("Cannot invoke non-function node: " + functionNode, this);
      }
    }

    /*
     * The number of arguments is constant for one invoke node. During compilation, the loop is
     * unrolled and the execute methods of all arguments are inlined. This is triggered by the
     * ExplodeLoop annotation on the method. The compiler assertion below illustrates that the
     * array length is really constant.
     */
    CompilerAsserts.compilationConstant(argumentNodes.length);
    CompilerAsserts.compilationConstant(this.isTail());

    if (argumentNodes.length > function.getCardinality()) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new YattaException("Unexpected number of arguments when calling '" + function.getName() +
          "': " + argumentNodes.length + " expected: " + function.getCardinality(), this);
    } else if (argumentNodes.length < function.getCardinality()) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      /*
       * Create a closure for partially applied function
       */
      String partiallyAppliedFunctionName = "$partial-" + argumentNodes.length + "/" + function.getCardinality() + "-" + function.getName();
      ExpressionNode[] allArgumentNodes = new ExpressionNode[function.getCardinality()];

      for (int i = 0; i < argumentNodes.length; i++) {
        /*
         * These arguments are already on the stack, so we just copy them
         */
        allArgumentNodes[i] = argumentNodes[i];
      }

      for (int i = argumentNodes.length, j = 0; i < function.getCardinality(); i++, j++) {
        /*
         * These are the new arguments, to be read on the actual application of this new closure
         */
        allArgumentNodes[i] = new ReadArgumentNode(j);
      }

      /*
       * Partially applied function will just invoke the original function with arguments constructed as a combination
       * of those which were provided when this closure was created and those to be read on the following application
       */
      InvokeNode invokeNode = new InvokeNode(language, new SimpleIdentifierNode(function.getName()), allArgumentNodes);
      BlockNode blockNode = new BlockNode(new ExpressionNode[]{
          /*
           * We need to make sure that the original function is still accessible within the closure, even if the partially
           * applied function already leaves the scope with the original function
          */
          WriteLocalVariableNodeGen.create(functionNode, frame.getFrameDescriptor().findOrAddFrameSlot(function.getName())),
          invokeNode
      });

      FunctionNode partiallyAppliedFunctionNode = new FunctionNode(language, getSourceSection(), partiallyAppliedFunctionName,
          function.getCardinality() - argumentNodes.length, frame.getFrameDescriptor(), blockNode);
      return partiallyAppliedFunctionNode.executeGeneric(frame);
    } else {
      Object[] argumentValues = new Object[argumentNodes.length];
      boolean argsArePromise = false;
      for (int i = 0; i < argumentNodes.length; i++) {
        Object argValue = argumentNodes[i].executeGeneric(frame);
        if (argValue instanceof Promise) {
            argsArePromise = true;
        }
        argumentValues[i] = argValue;
      }

      if (argsArePromise) {
        Promise argsPromise = Promise.all(argumentValues, this);
        return argsPromise.map(argValues -> {
          try {
            return library.execute(function, (Object[]) argValues);
          } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
            /* Execute was not successful. */
            return UndefinedNameException.undefinedFunction(this, function);
          }
        }, this);
      }

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

  @Override
  public boolean hasTag(Class<? extends Tag> tag) {
    if (tag == StandardTags.CallTag.class) {
      return true;
    }
    return super.hasTag(tag);
  }
}
