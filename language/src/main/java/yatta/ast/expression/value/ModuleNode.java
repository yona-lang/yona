package yatta.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.AliasNode;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.Module;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

@NodeInfo
public final class ModuleNode extends ExpressionNode {
  @Node.Child
  private FQNNode moduleFQN;
  @Node.Child
  private NonEmptyStringListNode exports;
  @Node.Children
  private FunctionLikeNode[] functions;
  @Child
  private ExpressionNode expression;

  private final Context context;

  public ModuleNode(FQNNode moduleFQN, NonEmptyStringListNode exports, FunctionLikeNode[] functions) {
    this.moduleFQN = moduleFQN;
    this.exports = exports;
    this.functions = functions;
    this.context = Context.getCurrent();
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
      throw new YattaException("Unable to load Module " + moduleFQN, ex, this);
    }
  }

  @Override
  public String executeString(VirtualFrame frame) throws UnexpectedResultException {
    return moduleFQN.executeString(frame);
  }

  @Override
  public Module executeModule(VirtualFrame frame) throws UnexpectedResultException {
    String executedModuleFQN = moduleFQN.executeString(frame);
    List<String> executedExports = exports.executeStringList(frame).asJavaList();
    List<Function> executedFunctions = new ArrayList<>(functions.length);

    /*
     * Set up module-local scope by putting all local functions on the stack
     */
    for (FunctionLikeNode fun : functions) {
      AliasNode aliasNode = new AliasNode(fun.name(), fun);
      aliasNode.executeGeneric(frame);
    }

    for (FunctionLikeNode fun : functions) {
      executedFunctions.add(fun.executeFunction(frame));
    }

    Module module = new Module(executedModuleFQN, executedExports, executedFunctions);
    context.cacheModule(executedModuleFQN, module);

    return module;
  }
}
