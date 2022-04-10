package yona.ast.expression.aliasTree;

import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.frame.VirtualFrame;
import yona.YonaLanguage;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.expression.PatternLetNode;
import yona.runtime.Seq;
import yona.runtime.Set;
import yona.runtime.async.Promise;

import java.util.Arrays;

public class AliasTreeBatchNode extends AliasTreeNode {
  @Children
  private AliasNode[] aliasNodes;

  private final TruffleLogger LOGGER = YonaLanguage.getLogger(AliasTreeBatchNode.class);

  public AliasTreeBatchNode(final AliasTreeNode parent, final AliasNode[] aliasNodes) {
    LOGGER.finest("Batch.new: " + Arrays.toString(aliasNodes));
    this.parentNode = parent;
    this.aliasNodes = aliasNodes;
  }

  @Override
  public String toString() {
    return "AliasTreeBatchNode{" +
        "parent=" + parentNode +
        ", aliasNodes=" + Arrays.toString(aliasNodes) +
        '}';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AliasTreeBatchNode that = (AliasTreeBatchNode) o;
    return Arrays.equals(aliasNodes, that.aliasNodes);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(aliasNodes);
  }

  @Override
  public Set alreadyProvidedDependencies() {
    Set result = parentNode.alreadyProvidedDependencies();

    for (AliasNode aliasNode : aliasNodes) {
      result = result.union(Set.set((Object[]) aliasNode.getProvidedIdentifiers()));
    }

    return result;
  }

  public AliasTreeNode chain(final AliasNode aliasNode) {
    LOGGER.finest("Batch.chain: " + aliasNode);
    LOGGER.finest("Batch.alreadyProvidedDependencies: " + alreadyProvidedDependencies());
    Set requiredDependencies = requiredDependenciesFor(aliasNode);
    LOGGER.finest("Batch.requiredDependencies: " + requiredDependencies);
    LOGGER.finest("Batch.requiredDependencies.intersection(alreadyProvidedDependencies()): " + requiredDependencies.intersection(alreadyProvidedDependencies()));

    if (requiredDependencies.intersection(alreadyProvidedDependencies()).size() == 0) { // does not depend on anything in the current batch
      AliasNode[] newAliases = new AliasNode[aliasNodes.length + 1];
      System.arraycopy(aliasNodes, 0, newAliases, 0, aliasNodes.length);
      newAliases[aliasNodes.length] = aliasNode;

      aliasNodes = newAliases;
      return this;
    } else {
      return new AliasTreeSingletonNode(this, aliasNode);
    }
  }

  @Override
  public Seq foldAliasNodes() {
    return parentNode.foldAliasNodes().insertLast(aliasNodes);
  }

  private Object executeAliases(final VirtualFrame frame, final ExpressionNode node) {
    Object result = null;

    for (AliasNode aliasNode : aliasNodes) {
      LOGGER.finest("Batch.execute.alias.aliasNode: " + aliasNode);
      LOGGER.finest("Batch.execute.alias.acc: " + result);

      if (result instanceof final Promise promise) {
        result = promise.map(unit -> aliasNode.executeGeneric(frame), node);
      } else {
        result = aliasNode.executeGeneric(frame);
        LOGGER.finest("Batch.execute.alias.result: " + result);
      }

    }

    return result;
  }

  @Override
  public Object executeGeneric(final VirtualFrame frame) {
    final Object parentResult = parentNode.executeGeneric(frame);
    LOGGER.finest("Batch.execute.aliasNodes: " + Arrays.toString(aliasNodes));
    LOGGER.finest("Batch.execute.parentResult: " + parentResult);

    if (parentResult instanceof final Promise parentPromise) {
      Object result = parentPromise.map(ignore -> executeAliases(frame, parentNode), parentNode);
      LOGGER.finest("Batch.execute.result: " + result);
      return result;
    } else {
      Object result = executeAliases(frame, parentNode);
      LOGGER.finest("Batch.execute.result: " + result);
      return result;
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    Seq result = Seq.EMPTY;

    for (AliasNode aliasNode : aliasNodes) {
      result = Seq.catenate(result, Seq.sequence((Object[]) aliasNode.getRequiredIdentifiers()));
    }

    return result.toArray(String.class);
  }
}
