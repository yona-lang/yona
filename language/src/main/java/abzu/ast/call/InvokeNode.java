package abzu.ast.call;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.instrumentation.Tag;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;

/**
 * The node for function invocation in Abzu. Since Abzu has first class functions, the {@link abzu.runtime.Function
 * target function} can be computed by an arbitrary expression. This node is responsible for
 * evaluating this expression, as well as evaluating the {@link #argumentNodes arguments}. The
 * actual dispatch is then delegated to a chain of {@link DispatchNode} that form a polymorphic
 * inline cache.
 */
@NodeInfo(shortName = "invoke")
public final class InvokeNode extends ExpressionNode {

  @Node.Child private ExpressionNode functionNode;
  @Node.Children private final ExpressionNode[] argumentNodes;
  @Node.Child private DispatchNode dispatchNode;

  public InvokeNode(ExpressionNode functionNode, ExpressionNode[] argumentNodes) {
    this.functionNode = functionNode;
    this.argumentNodes = argumentNodes;
    this.dispatchNode = DispatchNodeGen.create();
  }

  @ExplodeLoop
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object function = functionNode.executeGeneric(frame);

    /*
     * The number of arguments is constant for one invoke node. During compilation, the loop is
     * unrolled and the execute methods of all arguments are inlined. This is triggered by the
     * ExplodeLoop annotation on the method. The compiler assertion below illustrates that the
     * array length is really constant.
     */
    CompilerAsserts.compilationConstant(argumentNodes.length);

    Object[] argumentValues = new Object[argumentNodes.length];
    for (int i = 0; i < argumentNodes.length; i++) {
      argumentValues[i] = argumentNodes[i].executeGeneric(frame);
    }
    return dispatchNode.executeDispatch(function, argumentValues);
  }

  @Override
  public boolean hasTag(Class<? extends Tag> tag) {
    if (tag == StandardTags.CallTag.class) {
      return true;
    }
    return super.hasTag(tag);
  }
}
