package yona.runtime;

import com.oracle.truffle.api.nodes.Node;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;

import java.util.Arrays;
import java.util.Iterator;

public final class DependencyUtils {
  public static String[] catenateRequiredIdentifiersWith(ExpressionNode node1, ExpressionNode... nodes) {
    return catenateRequiredIdentifiers(node1.getRequiredIdentifiers(), nodes);
  }

  public static String[] catenateRequiredIdentifiers(ExpressionNode... nodes) {
    return ArrayUtils.catenateMany(nodes, ExpressionNode::getRequiredIdentifiers);
  }

  public static String[] catenateRequiredIdentifiers(Iterable<Node> nodes) {
    String[] ret = new String[0];
    Iterator<Node> iterator = nodes.iterator();
    while (iterator.hasNext()) {
      ret = ArrayUtils.catenate(ret, ((ExpressionNode) iterator.next()).getRequiredIdentifiers());
    }
    return ret;
  }

  public static String[] catenateRequiredIdentifiers(String[] initialIdentifiers, ExpressionNode... nodes) {
    return ArrayUtils.catenate(initialIdentifiers, ArrayUtils.catenateMany(nodes, ExpressionNode::getRequiredIdentifiers));
  }

  public static String[] catenateProvidedIdentifiers(AliasNode... nodes) {
    return ArrayUtils.catenateMany(nodes, AliasNode::getProvidedIdentifiers);
  }

  public static String[] catenateProvidedIdentifiers(AliasNode[] nodes1, AliasNode[] nodes2) {
    return ArrayUtils.catenate(
        ArrayUtils.catenateMany(nodes1, AliasNode::getProvidedIdentifiers),
        ArrayUtils.catenateMany(nodes2, AliasNode::getProvidedIdentifiers)
    );
  }

  public static String[] catenateProvidedIdentifiers(String[] initialIdentifiers, AliasNode... nodes) {
    return ArrayUtils.catenate(initialIdentifiers, ArrayUtils.catenateMany(nodes, AliasNode::getProvidedIdentifiers));
  }
}
