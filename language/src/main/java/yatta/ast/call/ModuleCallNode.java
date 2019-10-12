package yatta.ast.call;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.value.FQNNode;
import yatta.runtime.Function;
import yatta.runtime.Module;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class ModuleCallNode extends ExpressionNode {
  @Child
  private ExpressionNode nameNode;
  @Children
  private ExpressionNode[] argumentNodes;
  private String functionName;
  @Children private FQNNode[] moduleStack;
  private final YattaLanguage language;

  public ModuleCallNode(YattaLanguage language, ExpressionNode nameNode, String functionName, ExpressionNode[] argumentNodes, FQNNode[] moduleStack) {
    this.language = language;
    this.nameNode = nameNode;
    this.functionName = functionName;
    this.argumentNodes = argumentNodes;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ModuleCallNode that = (ModuleCallNode) o;
    return Objects.equals(nameNode, that.nameNode) &&
           Arrays.equals(argumentNodes, that.argumentNodes);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(nameNode);
    result = 31 * result + Arrays.hashCode(argumentNodes);
    return result;
  }

  @Override
  public String toString() {
    return "ModuleCallNode{" +
           "nameNode=" + nameNode +
           ", functionName=" + functionName +
           ", argumentNodes=" + Arrays.toString(argumentNodes) +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Module module;
    try {
      module = nameNode.executeModule(frame);
    } catch (UnexpectedResultException ex) {
      throw new YattaException("Unexpected error while invoking a module function: " + ex.getMessage(), ex, this);
    }

    if (!module.getExports().contains(functionName)) {
      throw new YattaException("Function " + functionName + " is not present in " + module, this);
    } else {
      Function function = module.getFunctions().get(functionName);
      InvokeNode invokeNode = new InvokeNode(language, function, argumentNodes, moduleStack);
      return invokeNode.executeGeneric(frame);
    }
  }
}
