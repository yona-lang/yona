package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.runtime.*;

import java.util.Arrays;

public final class DictMatchNode extends MatchNode {
  @Children
  ExpressionNode[] expressionNodes;
  @Children
  MatchNode[] patternNodes;

  public DictMatchNode(ExpressionNode[] expressionNodes, MatchNode[] patternNodes) {
    this.expressionNodes = expressionNodes;
    this.patternNodes = patternNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    DictMatchNode that = (DictMatchNode) o;
    return Arrays.equals(expressionNodes, that.expressionNodes) &&
        Arrays.equals(patternNodes, that.patternNodes);
  }

  @Override
  public int hashCode() {
    int result = Arrays.hashCode(expressionNodes);
    result = 31 * result + Arrays.hashCode(patternNodes);
    return result;
  }

  @Override
  public String toString() {
    return "DictMatchNode{" +
        "expressionNodes=" + Arrays.toString(expressionNodes) +
        ", patternNodes=" + Arrays.toString(patternNodes) +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Dict) {
      Dict dictionary = (Dict) value;

      if (expressionNodes.length == 0) {
        if (dictionary.size() == 0) {
          return MatchResult.TRUE;
        } else {
          return MatchResult.FALSE;
        }
      }

      Seq aliases = Seq.EMPTY;
      for (int i = 0; i < expressionNodes.length; i++) {
        Object key = expressionNodes[i].executeGeneric(frame);

        Object retrievedValue = dictionary.lookup(key);
        if (Unit.INSTANCE == retrievedValue) {
          return MatchResult.FALSE;
        } else {
          MatchResult matchResult = patternNodes[i].match(retrievedValue, frame);
          if (!matchResult.isMatches()) {
            return MatchResult.FALSE;
          } else {
            aliases = Seq.catenate(aliases, Seq.sequence((Object[]) matchResult.getAliases()));
          }
        }
      }

      aliases.foldLeft(null, (acc, alias) -> {
        ((AliasNode) alias).executeGeneric(frame);
        return null;
      });

      return MatchResult.TRUE;
    }

    return MatchResult.FALSE;
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(expressionNodes);
  }

  @Override
  protected String[] providedIdentifiers() {
    return ArrayUtils.catenate(
        DependencyUtils.catenateRequiredIdentifiers(expressionNodes),
        DependencyUtils.catenateProvidedIdentifiers(patternNodes)
    );
  }
}
