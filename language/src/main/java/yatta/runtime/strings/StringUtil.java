package yatta.runtime.strings;

import com.oracle.truffle.api.CompilerDirectives;
import yatta.runtime.Seq;
import yatta.runtime.Symbol;
import yatta.runtime.Unit;

public final class StringUtil {
  @CompilerDirectives.TruffleBoundary
  public static Seq yattaValueAsYattaString(boolean val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  @CompilerDirectives.TruffleBoundary
  public static Seq yattaValueAsYattaString(byte val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  @CompilerDirectives.TruffleBoundary
  public static Seq yattaValueAsYattaString(long val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  @CompilerDirectives.TruffleBoundary
  public static Seq yattaValueAsYattaString(double val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  public static Seq yattaValueAsYattaString(int val) {
    return Seq.sequence(val);
  }

  public static Seq yattaValueAsYattaString(Object val) {
    if (val instanceof Seq && ((Seq) val).isString()) return (Seq) val;
    else if (val instanceof Boolean) return yattaValueAsYattaString((boolean) val);
    else if (val instanceof Byte) return yattaValueAsYattaString((byte) val);
    else if (val instanceof Long) return yattaValueAsYattaString((long) val);
    else if (val instanceof Double) return yattaValueAsYattaString((double) val);
    else if (val instanceof Integer) return yattaValueAsYattaString((int) val);
    else if (val instanceof String) return Seq.fromCharSequence((String) val);
    else if (val instanceof Symbol) return Seq.fromCharSequence(((Symbol) val).asString());
    else return Seq.fromCharSequence(val.toString());
  }
}
