package yona.ast.expression;

import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.expression.aliasTree.AliasTreeNode;
import yona.runtime.Seq;
import yona.runtime.async.Promise;

import java.util.Objects;

@NodeInfo(shortName = "patternLet")
public final class PatternLetNode extends LexicalScopeNode {
  @Child
  public AliasTreeNode aliasTreeNode;
  @Child
  public ExpressionNode expression;

  public PatternLetNode(AliasTreeNode aliasTreeNode, ExpressionNode expression) {
    this.aliasTreeNode = aliasTreeNode;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    PatternLetNode that = (PatternLetNode) o;
    return Objects.equals(aliasTreeNode, that.aliasTreeNode) && Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(aliasTreeNode, expression);
  }

  @Override
  public String toString() {
    return "PatternLetNode{" +
        "aliasTreeNode=" + aliasTreeNode +
        ", expression=" + expression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.expression.setIsTail(isTail);
    this.aliasTreeNode.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    MaterializedFrame materializedFrame = frame.materialize();

    var result = aliasTreeNode.executeGeneric(materializedFrame);

    if (result instanceof Promise resultPromise) {
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

  public static AliasTreeNode resolveDependencies(Seq aliasNodes, yona.runtime.Set providedIdentifiers) {
    return aliasNodes.foldLeft(AliasTreeNode.root(providedIdentifiers), (node, aliasNodeObj) -> node.chain((AliasNode) aliasNodeObj));
  }
}
