package yatta.ast.expression;

import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.value.FQNNode;
import yatta.runtime.Function;
import yatta.runtime.Module;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Objects;

public class FunctionIdentifierNode extends ExpressionNode {
  @Child
  public FQNNode fqnNode;
  private final String functionName;

  public FunctionIdentifierNode(FQNNode fqnNode, String functionName) {
    this.fqnNode = fqnNode;
    this.functionName = functionName;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FunctionIdentifierNode that = (FunctionIdentifierNode) o;
    return Objects.equals(fqnNode, that.fqnNode) &&
        Objects.equals(functionName, that.functionName);
  }

  @Override
  public int hashCode() {
    return Objects.hash(fqnNode, functionName);
  }

  @Override
  public String toString() {
    return "FunctionIdentifierNode{" +
        "fqnNode=" + fqnNode +
        ", functionName='" + functionName + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      return executeFunction(frame);
    } catch (UnexpectedResultException e) {
      return null;
    }
  }

  @Override
  public Function executeFunction(VirtualFrame frame) throws UnexpectedResultException {
    Module module = fqnNode.executeModule(frame);

    if (!module.getExports().contains(functionName)) {
      throw new YattaException("Function " + functionName + " is not exported from module " + module, this);
    }

    return module.getFunctions().get(functionName);
  }
}
