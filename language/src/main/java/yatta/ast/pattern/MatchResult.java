package yatta.ast.pattern;

import yatta.ast.AliasNode;

import java.util.Arrays;
import java.util.Objects;

/**
 * Aliases are executed only after full match found.
 */
public final class MatchResult {
  public static final MatchResult FALSE = new MatchResult(false);
  public static final MatchResult TRUE = new MatchResult(true);

  private boolean matches;
  private AliasNode[] aliases;

  public MatchResult(boolean matches) {
    this.matches = matches;
    this.aliases = new AliasNode[0];
  }

  public MatchResult(boolean matches, AliasNode[] aliases) {
    this.matches = matches;
    this.aliases = aliases;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    MatchResult that = (MatchResult) o;
    return matches == that.matches &&
        Arrays.equals(aliases, that.aliases);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(matches);
    result = 31 * result + Arrays.hashCode(aliases);
    return result;
  }

  @Override
  public String toString() {
    return "MatchResult{" +
        "matches=" + matches +
        ", aliases=" + Arrays.toString(aliases) +
        '}';
  }

  public boolean isMatches() {
    return matches;
  }

  public AliasNode[] getAliases() {
    return aliases;
  }
}
