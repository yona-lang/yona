package yona.ast.expression.aliasTree;

import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.frame.VirtualFrame;
import yona.YonaLanguage;
import yona.ast.AliasNode;
import yona.ast.expression.PatternLetNode;
import yona.runtime.Seq;
import yona.runtime.Set;
import yona.runtime.Unit;

import java.util.Objects;

public final class AliasNodeRootNode extends AliasTreeNode {
  final private Set alreadyProvidedDependencies;

  private final TruffleLogger LOGGER = YonaLanguage.getLogger(AliasNodeRootNode.class);

  public AliasNodeRootNode(final Set alreadyProvidedDependencies) {
    LOGGER.finest("Root.new: " + alreadyProvidedDependencies);
    this.parentNode = null;
    this.alreadyProvidedDependencies = alreadyProvidedDependencies;
  }

  @Override
  public String toString() {
    return "AliasNodeRootNode{" +
        "alreadyProvidedDependencies=" + alreadyProvidedDependencies +
        '}';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AliasNodeRootNode that = (AliasNodeRootNode) o;
    return Objects.equals(alreadyProvidedDependencies, that.alreadyProvidedDependencies);
  }

  @Override
  public int hashCode() {
    return Objects.hash(alreadyProvidedDependencies);
  }

  @Override
  public Set alreadyProvidedDependencies() {
    return alreadyProvidedDependencies;
  }

  @Override
  public AliasTreeNode chain(final AliasNode aliasNode) {
    return new AliasTreeSingletonNode(this, aliasNode);
  }

  @Override
  public Seq foldAliasNodes() {
    return Seq.EMPTY;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return Unit.INSTANCE;
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }
}
