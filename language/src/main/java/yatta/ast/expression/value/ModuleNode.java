package yatta.ast.expression.value;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.NameAliasNode;
import yatta.runtime.Dict;
import yatta.runtime.Function;
import yatta.runtime.YattaModule;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

@NodeInfo(shortName = "module")
public final class ModuleNode extends ExpressionNode {
  @Node.Child
  private FQNNode moduleFQN;
  @Node.Child
  private NonEmptyStringListNode exports;
  @Node.Children
  private FunctionLikeNode[] functions;
  private final Dict records;  // <String, String[]>

  public ModuleNode(FQNNode moduleFQN, NonEmptyStringListNode exports, FunctionLikeNode[] functions, Dict records) {
    this.moduleFQN = moduleFQN;
    this.exports = exports;
    this.functions = functions;
    this.records = records;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ModuleNode that = (ModuleNode) o;
    return Objects.equals(moduleFQN, that.moduleFQN) &&
        Objects.equals(exports, that.exports) &&
        Arrays.equals(functions, that.functions) &&
        Objects.equals(records, that.records);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(moduleFQN, exports);
    result = 31 * result + Arrays.hashCode(functions);
    result = 31 * result + Objects.hashCode(records);
    return result;
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
  public YattaModule executeModule(VirtualFrame frame) throws UnexpectedResultException {
    String executedModuleFQN = moduleFQN.executeString(frame);
    List<String> executedExports = exports.executeStringList(frame).asJavaList();
    CompilerDirectives.transferToInterpreterAndInvalidate();
    List<Function> executedFunctions = new ArrayList<>(functions.length);

    /*
     * Set up module-local scope by putting all local functions on the stack
     */
    for (FunctionLikeNode fun : functions) {
      NameAliasNode nameAliasNode = new NameAliasNode(fun.name(), fun);
      nameAliasNode.executeGeneric(frame);
    }

    for (FunctionLikeNode fun : functions) {
      executedFunctions.add(fun.executeFunction(frame));
    }

    YattaModule module = new YattaModule(executedModuleFQN, executedExports, executedFunctions, records);
    lookupContextReference(YattaLanguage.class).get().cacheModule(executedModuleFQN, module);

    return module;
  }
}
