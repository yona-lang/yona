package yona.ast.generators;

public enum GeneratedCollection {
  SEQ, SET, DICT;

  public String toLowerString() {
    return switch (this) {
      case SEQ -> "seq";
      case SET -> "set";
      case DICT -> "dict";
    };
  }

  public String reducerForGeneratedCollection() {
    return switch (this) {
      case SEQ -> "to_seq";
      case SET -> "to_set";
      case DICT -> "to_dict";
    };
  }
}
