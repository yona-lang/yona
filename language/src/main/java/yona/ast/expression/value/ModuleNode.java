package yona.ast.expression.value;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.ExpressionNode;
import yona.ast.expression.NameAliasNode;
import yona.runtime.DependencyUtils;
import yona.runtime.Dict;
import yona.runtime.Function;
import yona.runtime.YonaModule;

import java.util.*;

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
  public String toString() {
    return "ModuleNode{" +
           "moduleFQN=" + moduleFQN +
           ", exports=" + exports +
           ", functions=" + Arrays.toString(functions) +
           ", records=" + records +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      return executeModule(frame);
    } catch (UnexpectedResultException ex) {
      throw new YonaException("Unable to load module " + moduleFQN, ex, this);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(functions);
  }

  @Override
  public String executeString(VirtualFrame frame) throws UnexpectedResultException {
    return moduleFQN.executeString(frame);
  }

  @Override
  public YonaModule executeModule(VirtualFrame frame) throws UnexpectedResultException {
    String executedModuleFQN = moduleFQN.executeString(frame);
    Set<String> executedExports = exports.executeStringList(frame).asJavaSet();
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

    YonaModule module = new YonaModule(executedModuleFQN, executedExports, executedFunctions, records);
    lookupContextReference(YonaLanguage.class).get().cacheModule(executedModuleFQN, module);

    return module;
  }
}
