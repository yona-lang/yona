package yona.ast.generators;

public enum GeneratedCollection {
  SEQ, SET, DICT;

  public String toLowerString() {
    switch (this) {
      case SEQ: return "seq";
      case SET: return "set";
      case DICT: return "dict";
    }
    return null;  // wtf is this required
  }
}
