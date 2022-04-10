package yona.ast.expression.aliasTree;

import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.frame.VirtualFrame;
import yona.YonaLanguage;
import yona.ast.AliasNode;
import yona.runtime.Seq;
import yona.runtime.Set;
import yona.runtime.async.Promise;

import java.util.Objects;

public class AliasTreeSingletonNode extends AliasTreeNode {
  @Child
  private AliasNode aliasNode;

  private final TruffleLogger LOGGER = YonaLanguage.getLogger(AliasTreeSingletonNode.class);

  public AliasTreeSingletonNode(final AliasTreeNode parent, final AliasNode aliasNode) {
    LOGGER.finest("Singleton.new: " + aliasNode);
    this.parentNode = parent;
    this.aliasNode = aliasNode;
  }

  @Override
  public String toString() {
    return "AliasTreeSingletonNode{" +
        "parent=" + parentNode +
        ", aliasNode=" + aliasNode +
        '}';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AliasTreeSingletonNode that = (AliasTreeSingletonNode) o;
    return Objects.equals(aliasNode, that.aliasNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(aliasNode);
  }

  @Override
  public Set alreadyProvidedDependencies() {
    return Set.set((Object[]) aliasNode.getProvidedIdentifiers());
  }

  public AliasTreeNode chain(final AliasNode aliasNode) {
    LOGGER.finest("Singleton.chain: " + aliasNode);
    LOGGER.finest("Singleton.alreadyProvidedDependencies: " + alreadyProvidedDependencies());
    LOGGER.finest("Singleton.parent.alreadyProvidedDependencies: " + parentNode.alreadyProvidedDependencies());
    Set requiredDependencies = requiredDependenciesFor(aliasNode).difference(parentNode.alreadyProvidedDependencies());
    LOGGER.finest("Singleton.requiredDependenciesRaw: " + requiredDependenciesFor(aliasNode));
    LOGGER.finest("Singleton.requiredDependencies: " + requiredDependencies);

    if (requiredDependencies.size() == 0) {
      return new AliasTreeBatchNode(parentNode, new AliasNode[]{this.aliasNode, aliasNode});
    } else {
      return new AliasTreeSingletonNode(this, aliasNode);
    }
  }

  @Override
  public Seq foldAliasNodes() {
    return parentNode.foldAliasNodes().insertLast(aliasNode);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    final Object parentResult = parentNode.executeGeneric(frame);
    LOGGER.finest("Singleton.execute.aliasNode: " + aliasNode);
    LOGGER.finest("Singleton.execute.parentResult: " + parentResult);

    if (parentResult instanceof final Promise parentPromise) {
      Object result = parentPromise.map(unit -> aliasNode.executeGeneric(frame), aliasNode);
      LOGGER.finest("Singleton.execute.result: " + result);
      return result;
    } else {
      Object result = aliasNode.executeGeneric(frame);
      LOGGER.finest("Singleton.execute.result: " + result);
      return result;
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return aliasNode.getRequiredIdentifiers();
  }
}
