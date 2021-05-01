package yona.runtime.strings;

import com.oracle.truffle.api.CompilerDirectives;
import yona.runtime.NativeObject;
import yona.runtime.Seq;
import yona.runtime.Symbol;

public final class StringUtil {
  @CompilerDirectives.TruffleBoundary
  public static Seq yonaValueAsYonaString(boolean val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  @CompilerDirectives.TruffleBoundary
  public static Seq yonaValueAsYonaString(byte val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  @CompilerDirectives.TruffleBoundary
  public static Seq yonaValueAsYonaString(long val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  @CompilerDirectives.TruffleBoundary
  public static Seq yonaValueAsYonaString(double val) {
    return Seq.fromCharSequence(String.valueOf(val));
  }

  public static Seq yonaValueAsYonaString(int val) {
    return Seq.sequence(val);
  }

  public static Seq yonaValueAsYonaString(Object val) {
    if (val instanceof Seq && ((Seq) val).isString()) return (Seq) val;
    else if (val instanceof Boolean) return yonaValueAsYonaString((boolean) val);
    else if (val instanceof Byte) return yonaValueAsYonaString((byte) val);
    else if (val instanceof Long) return yonaValueAsYonaString((long) val);
    else if (val instanceof Double) return yonaValueAsYonaString((double) val);
    else if (val instanceof Integer) return yonaValueAsYonaString((int) val);
    else if (val instanceof String) return Seq.fromCharSequence((String) val);
    else if (val instanceof Symbol) return Seq.fromCharSequence(((Symbol) val).asString());
    else return Seq.fromCharSequence(val.toString());
  }
}
