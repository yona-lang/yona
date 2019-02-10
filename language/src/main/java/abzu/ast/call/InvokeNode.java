package abzu.ast.call;

import abzu.AbzuException;
import abzu.AbzuLanguage;
import abzu.ast.ExpressionNode;
import abzu.ast.controlflow.BlockNode;
import abzu.ast.expression.SimpleIdentifierNode;
import abzu.ast.expression.value.FunctionNode;
import abzu.ast.local.ReadArgumentNode;
import abzu.ast.local.WriteLocalVariableNodeGen;
import abzu.runtime.Function;
import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.instrumentation.Tag;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Arrays;

/**
 * The node for function invocation in Abzu. Since Abzu has first class functions, the {@link abzu.runtime.Function
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
  @Node.Child
  private DispatchNode dispatchNode;

  private AbzuLanguage language;

  public InvokeNode(AbzuLanguage language, ExpressionNode functionNode, ExpressionNode[] argumentNodes) {
    this.functionNode = functionNode;
    this.function = null;
    this.argumentNodes = argumentNodes;
    this.dispatchNode = DispatchNodeGen.create();
    this.language = language;
  }

  public InvokeNode(AbzuLanguage language, Function function, ExpressionNode[] argumentNodes) {
    this.functionNode = null;
    this.function = function;
    this.argumentNodes = argumentNodes;
    this.dispatchNode = DispatchNodeGen.create();
    this.language = language;
  }

  @ExplodeLoop
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Function function;
    if (this.function != null) {
      function = this.function;
    } else {
      try {
        function = functionNode.executeFunction(frame);
      } catch (UnexpectedResultException e) {
        throw new AbzuException("Cannot invoke non-function node: " + functionNode, this);
      }
    }

    /*
     * The number of arguments is constant for one invoke node. During compilation, the loop is
     * unrolled and the execute methods of all arguments are inlined. This is triggered by the
     * ExplodeLoop annotation on the method. The compiler assertion below illustrates that the
     * array length is really constant.
     */
    CompilerAsserts.compilationConstant(argumentNodes.length);

    if (argumentNodes.length > function.getCardinality()) {
      throw new AbzuException("Unexpected number of arguments when calling '" + function.getName() +
                              "': " + argumentNodes.length + " expected: " + function.getCardinality(), this);
    } else if (argumentNodes.length < function.getCardinality()) {
      /*
       * Create a closure for partially applied function
       */
      String partiallyAppliedFunctionName = "$partial-" + argumentNodes.length + "/" + function.getCardinality() + "-" + function.getName();
      ExpressionNode[] allArgumentNodes = new ExpressionNode[function.getCardinality()];

      for (int i = 0; i < argumentNodes.length; i++) {
        /*
         * These arguments are already on the stack, so we just create ident nodes for them
         */
        allArgumentNodes[i] = new ReadArgumentNode(i);
      }

      for (int i = argumentNodes.length - 1, j = 0; i < function.getCardinality(); i++, j++) {
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
          function.getCardinality() - argumentNodes.length + 1, frame.getFrameDescriptor(), blockNode);
      return partiallyAppliedFunctionNode.executeGeneric(frame);
    } else {
      Object[] argumentValues = new Object[argumentNodes.length];
      for (int i = 0; i < argumentNodes.length; i++) {
        argumentValues[i] = argumentNodes[i].executeGeneric(frame);
      }
      return dispatchNode.executeDispatch(function, argumentValues);
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
