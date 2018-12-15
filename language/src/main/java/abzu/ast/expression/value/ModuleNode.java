package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import abzu.ast.expression.AliasNode;
import abzu.ast.local.WriteLocalVariableNode;
import abzu.ast.local.WriteLocalVariableNodeGen;
import abzu.runtime.Function;
import abzu.runtime.Module;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

@NodeInfo
public final class ModuleNode extends ExpressionNode {
  @Node.Child
  public FQNNode moduleFQN;
  @Node.Child
  public NonEmptyStringListNode exports;
  @Node.Children
  public FunctionNode[] functions;

  @Child
  public ExpressionNode expression;

  public ModuleNode(FQNNode moduleFQN, NonEmptyStringListNode exports, FunctionNode[] functions) {
    this.moduleFQN = moduleFQN;
    this.exports = exports;
    this.functions = functions;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ModuleNode that = (ModuleNode) o;
    return Objects.equals(moduleFQN, that.moduleFQN) &&
           Objects.equals(exports, that.exports) &&
           Objects.equals(functions, that.functions);
  }

  @Override
  public int hashCode() {
    return Objects.hash(moduleFQN, exports, expression);
  }

  @Override
  public String toString() {
    return "ModuleNode{" +
           "moduleFQN='" + moduleFQN + '\'' +
           ", exports=" + exports +
           ", functions=" + Arrays.toString(functions) +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      return executeModule(frame);
    } catch (UnexpectedResultException ex) {
      return null;
    }
  }

  @Override
  public Module executeModule(VirtualFrame frame) throws UnexpectedResultException {
    List<String> executedModuleFQN = moduleFQN.executeStringList(frame).asJavaList();
    List<String> executedExports = exports.executeStringList(frame).asJavaList();
    List<Function> executedFunctions = new ArrayList<>(functions.length);

    /*
     * Set up module-local scope by putting all local functions on the stack
     */
    for (FunctionNode fun : functions) {
      AliasNode aliasNode = new AliasNode(fun.name, fun);
      aliasNode.executeGeneric(frame);
    }

    for (FunctionNode fun : functions) {
      executedFunctions.add(fun.executeFunction(frame));
    }

    return new Module(executedModuleFQN, executedExports, executedFunctions);
  }
}
