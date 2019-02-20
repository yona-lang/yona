package abzu.ast.call;

import abzu.AbzuException;
import abzu.AbzuLanguage;
import abzu.ast.ExpressionNode;
import abzu.ast.expression.value.FQNNode;
import abzu.runtime.Function;
import abzu.runtime.Module;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class ModuleCallNode extends ExpressionNode {
  @Child
  private FQNNode fqnNode;
  @Children
  private ExpressionNode[] argumentNodes;
  private String functionName;
  private final AbzuLanguage abzuLanguage;

  public ModuleCallNode(AbzuLanguage abzuLanguage, FQNNode fqnNode, String functionName, ExpressionNode[] argumentNodes) {
    this.abzuLanguage = abzuLanguage;
    this.fqnNode = fqnNode;
    this.functionName = functionName;
    this.argumentNodes = argumentNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ModuleCallNode that = (ModuleCallNode) o;
    return Objects.equals(fqnNode, that.fqnNode) &&
           Arrays.equals(argumentNodes, that.argumentNodes);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(fqnNode);
    result = 31 * result + Arrays.hashCode(argumentNodes);
    return result;
  }

  @Override
  public String toString() {
    return "ModuleCallNode{" +
           "fqnNode=" + fqnNode +
           ", functionName=" + functionName +
           ", argumentNodes=" + Arrays.toString(argumentNodes) +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Module module;
    try {
      module = fqnNode.executeModule(frame);
    } catch (UnexpectedResultException ex) {
      throw new AbzuException("Unexpected error while invoking a module function: " + ex.getMessage(), this);
    }

    if (!module.getExports().contains(functionName)) {
      throw new AbzuException("Function " + functionName + " is not present in " + module, this);
    } else {
      Function function = module.getFunctions().get(functionName);
      InvokeNode invokeNode = new InvokeNode(abzuLanguage, function, argumentNodes);
      return invokeNode.executeGeneric(frame);
    }
  }
}
