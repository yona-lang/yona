package yona.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Set;
import yona.runtime.Unit;
import yona.runtime.async.Promise;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo(shortName = "patternLet")
public final class PatternLetNode extends LexicalScopeNode {
  @Children
  public AliasNode[] patternAliases;
  @Child
  public ExpressionNode expression;

  public PatternLetNode(AliasNode[] patternAliases, ExpressionNode expression) {
    this.patternAliases = patternAliases;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    PatternLetNode letNode = (PatternLetNode) o;
    return Arrays.equals(patternAliases, letNode.patternAliases) &&
        Objects.equals(expression, letNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(Arrays.hashCode(patternAliases), expression);
  }

  @Override
  public String toString() {
    return "PatternLetNode{" +
        "patterns=" + Arrays.toString(patternAliases) +
        ", expression=" + expression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.expression.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Context context = lookupContextReference(YonaLanguage.class).get();

    CompilerDirectives.transferToInterpreterAndInvalidate();
    MaterializedFrame materializedFrame = frame.materialize();

    Object result = resolveDependencies(Seq.sequence((AliasNode[]) patternAliases), context.globallyProvidedIdentifiers()).executeGeneric(materializedFrame, this);

    if (result instanceof Promise) {
      Promise resultPromise = (Promise) result;
      if (resultPromise.isFulfilled()) {
        try {
          resultPromise.unwrapOrThrow();
        } catch (YonaException e) {
          throw e;
        } catch (Throwable e) {
          throw new YonaException(e, this);
        }
        return expression.executeGeneric(materializedFrame);
      } else {
        return resultPromise.map(unit -> expression.executeGeneric(materializedFrame), this);
      }
    } else {
      return expression.executeGeneric(materializedFrame);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return expression.getRequiredIdentifiers();
  }

  static AliasTree resolveDependencies(Seq aliasNodes, Set providedIdentifiers) {
    return aliasNodes.foldLeft(AliasTree.root(providedIdentifiers), (node, aliasNodeObj) -> node.chain((AliasNode) aliasNodeObj));
  }

  /**
   * Representation of dependent AliasNodes in the let expression. This is a base class.
   * Multiple AliasNodes that have distinct dependencies, ie do not depend on one another are wrapped as batches (@see AliasTreeBatchNode).
   * Remaining AliasNodes are represented as @see AliasTreeSingletonNode.
   */
  abstract static class AliasTree {
    AliasTree parent;

    public static AliasTree root(final Set alreadyProvidedDependencies) {
      return new AliasNodeRootNode(alreadyProvidedDependencies);
    }

    public abstract Set alreadyProvidedDependencies();

    public abstract AliasTree chain(final AliasNode aliasNode);

    protected Set requiredDependenciesFor(final AliasNode aliasNode) {
      return Set.set((String[]) aliasNode.getRequiredIdentifiers());
    }

    public abstract Seq foldAliasNodes();

    public abstract Object executeGeneric(final VirtualFrame frame, final ExpressionNode node);
  }

  static class AliasTreeSingletonNode extends AliasTree {
    final AliasNode aliasNode;

    private final TruffleLogger LOGGER = YonaLanguage.getLogger(AliasTreeSingletonNode.class);

    public AliasTreeSingletonNode(final AliasTree parent, final AliasNode aliasNode) {
      LOGGER.finest("Singleton.new: " + aliasNode);
      this.parent = parent;
      this.aliasNode = aliasNode;
    }

    @Override
    public String toString() {
      return "AliasTreeSingletonNode{" +
          "parent=" + parent +
          ", aliasNode=" + aliasNode +
          '}';
    }

    @Override
    public Set alreadyProvidedDependencies() {
      return Set.set((String[]) aliasNode.getProvidedIdentifiers());
    }

    public AliasTree chain(final AliasNode aliasNode) {
      LOGGER.finest("Singleton.chain: " + aliasNode);
      LOGGER.finest("Singleton.alreadyProvidedDependencies: " + alreadyProvidedDependencies());
      LOGGER.finest("Singleton.parent.alreadyProvidedDependencies: " + parent.alreadyProvidedDependencies());
      Set requiredDependencies = requiredDependenciesFor(aliasNode).difference(parent.alreadyProvidedDependencies());
      LOGGER.finest("Singleton.requiredDependenciesRaw: " + requiredDependenciesFor(aliasNode));
      LOGGER.finest("Singleton.requiredDependencies: " + requiredDependencies);

      if (requiredDependencies.size() == 0) {
        return new AliasTreeBatchNode(parent, Seq.sequence(this.aliasNode, aliasNode));
      } else {
        return new AliasTreeSingletonNode(this, aliasNode);
      }
    }

    @Override
    public Seq foldAliasNodes() {
      return parent.foldAliasNodes().insertLast(aliasNode);
    }

    @Override
    public Object executeGeneric(VirtualFrame frame, ExpressionNode node) {
      final Object parentResult = parent.executeGeneric(frame, node);
      LOGGER.fine("Singleton.execute.aliasNode: " + aliasNode);
      LOGGER.fine("Singleton.execute.parentResult: " + parentResult);

      if (parentResult instanceof Promise) {
        final Promise parentPromise = (Promise) parentResult;
        Object result = parentPromise.map(unit -> aliasNode.executeGeneric(frame), node);
        LOGGER.fine("Singleton.execute.result: " + result);
        return result;
      } else {
        Object result = aliasNode.executeGeneric(frame);
        LOGGER.fine("Singleton.execute.result: " + result);
        return result;
      }
    }
  }

  static class AliasTreeBatchNode extends AliasTree {
    Seq aliasNodes;

    private final TruffleLogger LOGGER = YonaLanguage.getLogger(AliasTreeBatchNode.class);

    public AliasTreeBatchNode(final AliasTree parent, final Seq aliasNodes) {
      LOGGER.finest("Batch.new: " + aliasNodes);
      this.parent = parent;
      this.aliasNodes = aliasNodes;
    }

    @Override
    public String toString() {
      return "AliasTreeBatchNode{" +
          "parent=" + parent +
          ", aliasNodes=" + aliasNodes +
          '}';
    }

    @Override
    public Set alreadyProvidedDependencies() {
      return parent.alreadyProvidedDependencies().union(aliasNodes.foldLeft(Set.empty(), (providedDependencies, aliasNodeObj) -> providedDependencies.union(Set.set((String[]) ((AliasNode) aliasNodeObj).getProvidedIdentifiers()))));
    }

    public AliasTree chain(final AliasNode aliasNode) {
      LOGGER.finest("Batch.chain: " + aliasNode);
      LOGGER.finest("Batch.alreadyProvidedDependencies: " + alreadyProvidedDependencies());
      Set requiredDependencies = requiredDependenciesFor(aliasNode);
      LOGGER.finest("Batch.requiredDependencies: " + requiredDependencies);
      LOGGER.finest("Batch.requiredDependencies.intersection(alreadyProvidedDependencies()): " + requiredDependencies.intersection(alreadyProvidedDependencies()));

      if (requiredDependencies.intersection(alreadyProvidedDependencies()).size() == 0) { // does not depend on anything in the current batch
        aliasNodes = aliasNodes.insertLast(aliasNode);
        return this;
      } else {
        return new AliasTreeSingletonNode(this, aliasNode);
      }
    }

    @Override
    public Seq foldAliasNodes() {
      return parent.foldAliasNodes().insertLast(aliasNodes.toArray(AliasNode.class));
    }

    private Object executeAliases(final VirtualFrame frame, final ExpressionNode node) {
      return aliasNodes.foldLeft(null, (acc, aliasNodeObj) -> {
        final AliasNode aliasNode = (AliasNode) aliasNodeObj;
        LOGGER.finest("Batch.execute.alias.aliasNode: " + aliasNode);
        LOGGER.finest("Batch.execute.alias.acc: " + acc);

        if (acc instanceof Promise) {
          final Promise promise = (Promise) acc;
          return promise.map(unit -> aliasNode.executeGeneric(frame), node);
        } else {
          Object result = aliasNode.executeGeneric(frame);
          LOGGER.finest("Batch.execute.alias.result: " + result);
          return result;
        }
      });
    }

    @Override
    public Object executeGeneric(final VirtualFrame frame, final ExpressionNode node) {
      final Object parentResult = parent.executeGeneric(frame, node);
      LOGGER.fine("Batch.execute.aliasNodes: " + aliasNodes);
      LOGGER.fine("Batch.execute.parentResult: " + parentResult);

      if (parentResult instanceof Promise) {
        final Promise parentPromise = (Promise) parentResult;
        Object result = parentPromise.map(ignore -> executeAliases(frame, node), node);
        LOGGER.fine("Batch.execute.result: " + result);
        return result;
      } else {
        Object result = executeAliases(frame, node);
        LOGGER.fine("Batch.execute.result: " + result);
        return result;
      }
    }
  }

  static final class AliasNodeRootNode extends AliasTree {
    final private Set alreadyProvidedDependencies;

    private final TruffleLogger LOGGER = YonaLanguage.getLogger(AliasNodeRootNode.class);

    public AliasNodeRootNode(final Set alreadyProvidedDependencies) {
      LOGGER.finest("Root.new: " + alreadyProvidedDependencies);
      this.parent = null;
      this.alreadyProvidedDependencies = alreadyProvidedDependencies;
    }

    @Override
    public String toString() {
      return "AliasNodeRootNode{" +
          "alreadyProvidedDependencies=" + alreadyProvidedDependencies +
          '}';
    }

    @Override
    public Set alreadyProvidedDependencies() {
      return alreadyProvidedDependencies;
    }

    @Override
    public AliasTree chain(final AliasNode aliasNode) {
      return new AliasTreeSingletonNode(this, aliasNode);
    }

    @Override
    public Seq foldAliasNodes() {
      return Seq.EMPTY;
    }

    @Override
    public Object executeGeneric(VirtualFrame frame, ExpressionNode node) {
      return Unit.INSTANCE;
    }
  }
}
