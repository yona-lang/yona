package yona.ast.call;

import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.ExpressionNode;
import yona.runtime.DependencyUtils;
import yona.runtime.Function;
import yona.runtime.async.Promise;

import java.util.Arrays;

/**
 * The node for function invocation in Yona. Since Yona has first class functions, the {@link yona.runtime.Function
 * target function} can be computed by an arbitrary expression. This node is responsible for
 * evaluating this expression, as well as evaluating the {@link #argumentNodes arguments}. The
 * actual dispatch is then delegated to a chain of {@link DispatchNode} that form a polymorphic
 * inline cache.
 */
@NodeInfo(shortName = "invoke")
public final class ExpressionInvokeNode extends InvokeNode {
  @Child
  private ExpressionNode functionNode;
  @Children
  private final ExpressionNode[] argumentNodes;
  @Children
  private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode | Because this is created from Stack.toArray, the last pushed element is the last element of the array
  @Child
  private InvokeNode invokeNode;

  private final YonaLanguage language;

  public ExpressionInvokeNode(YonaLanguage language, ExpressionNode functionNode, ExpressionNode[] argumentNodes, ExpressionNode[] moduleStack) {
    this.functionNode = functionNode;
    this.argumentNodes = argumentNodes;
    this.language = language;
    this.moduleStack = moduleStack;
  }

  @Override
  public String toString() {
    return "ExpressionInvokeNode{" +
        "functionNode=" + functionNode +
        ", argumentNodes=" + Arrays.toString(argumentNodes) +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object maybeFunction = functionNode.executeGeneric(frame);
    if (maybeFunction instanceof Function function) {
      invokeNode = new FunctionInvokeNode(language, function, argumentNodes, moduleStack);
      invokeNode.setIsTail(isTail());
      adoptChildren();
      return invokeNode.executeGeneric(frame);
    } else if (maybeFunction instanceof Promise promise) {
      MaterializedFrame materializedFrame = frame.materialize();
      return promise.map(value -> {
        if (value instanceof Function function) {
          invokeNode = new FunctionInvokeNode(language, function, argumentNodes, moduleStack);
          invokeNode.setIsTail(isTail());
          adoptChildren();
          return invokeNode.executeGeneric(materializedFrame);
        } else {
          throw notAFucntion(value);
        }
      }, this);
    } else {
      throw notAFucntion(maybeFunction);
    }
  }

  private RuntimeException notAFucntion(Object value) {
    return new YonaException("Cannot invoke non-function value: %s".formatted(value), this);
  }

  @Override
  public String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiersWith(functionNode, argumentNodes);
  }
}
