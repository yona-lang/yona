package yatta.ast;

public abstract class AliasNode extends ExpressionNode {
  /**
   * These are identifiers a pattern is providing. Essentially identifiers on a left side of a pattern expression.
   */
  protected abstract String[] providedIdentifiers();

  private String[] cachedProvidedIdentifiers = null;

  /**
   * These are identifiers a pattern is providing. Essentially identifiers on a left side of a pattern expression. Cached after first call.
   */
  public String[] getProvidedIdentifiers() {
    if (cachedProvidedIdentifiers == null) {
      cachedProvidedIdentifiers = providedIdentifiers();
      return cachedProvidedIdentifiers;
    } else {
      return cachedProvidedIdentifiers;
    }
  }
}
