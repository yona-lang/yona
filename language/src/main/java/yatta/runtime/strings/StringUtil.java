package yatta.runtime.strings;

import yatta.runtime.Seq;

public final class StringUtil {
  public static Seq yattaValueAsYattaString(boolean val) {
    return Seq.sequence(val);
  }

  public static Seq yattaValueAsYattaString(byte val) {
    return Seq.sequence(val);
  }

  public static Seq yattaValueAsYattaString(long val) {
    return Seq.sequence(val);
  }

  public static Seq yattaValueAsYattaString(double val) {
    return Seq.sequence(val);
  }

  public static Seq yattaValueAsYattaString(int val) {
    return Seq.sequence(val);
  }

  public static Seq yattaValueAsYattaString(String val) {
    return Seq.fromCharSequence(val);
  }

  public static Seq yattaValueAsYattaString(Object val) {
    if (val instanceof Seq) return (Seq) val;
    else return Seq.fromCharSequence(val.toString());
  }
}
