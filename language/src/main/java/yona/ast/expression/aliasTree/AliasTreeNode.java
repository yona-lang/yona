package yona.ast.expression.aliasTree;

import com.oracle.truffle.api.frame.VirtualFrame;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.expression.PatternLetNode;
import yona.runtime.Seq;
import yona.runtime.Set;

/**
 * Representation of dependent AliasNodes in the let expression. This is a base class.
 * Multiple AliasNodes that have distinct dependencies, ie do not depend on one another are wrapped as batches (@see AliasTreeBatchNode).
 * Remaining AliasNodes are represented as @see AliasTreeSingletonNode.
 */
public abstract class AliasTreeNode extends ExpressionNode {
  @Child
  protected AliasTreeNode parentNode;

  public static AliasTreeNode root(final Set alreadyProvidedDependencies) {
    return new AliasNodeRootNode(alreadyProvidedDependencies);
  }

  public abstract Set alreadyProvidedDependencies();

  public abstract AliasTreeNode chain(final AliasNode aliasNode);

  protected Set requiredDependenciesFor(final AliasNode aliasNode) {
    return Set.set((Object[]) aliasNode.getRequiredIdentifiers());
  }

  public abstract Seq foldAliasNodes();

  public abstract Object executeGeneric(final VirtualFrame frame);
}
