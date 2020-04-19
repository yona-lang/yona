package yatta.ast.builtin;

import yatta.YattaLanguage;
import yatta.runtime.NativeObject;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;
import com.oracle.truffle.api.CompilerDirectives.TruffleBoundary;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.runtime.Context;
import yatta.runtime.exceptions.BadArgException;

import java.io.PrintWriter;

/**
 * Builtin function to write a value to the {@link Context#getOutput() standard output}. The
 * different specialization leverage the typed {@code println} methods available in Java, i.e.,
 * primitive values are printed without converting them to a {@link String} first.
 * <p>
 * Printing involves a lot of Java code, so we need to tell the optimizing system that it should not
 * unconditionally inline everything reachable from the println() method. This is done via the
 * {@link TruffleBoundary} annotations.
 */
@NodeInfo(shortName = "println")
public abstract class PrintlnBuiltin extends BuiltinNode {

  @Specialization
  public long println(long value, @CachedContext(YattaLanguage.class) Context context) {
    doPrint(context.getOutput(), value);
    return value;
  }

  @TruffleBoundary
  private void doPrint(PrintWriter out, long value) {
    out.println(value);
  }

  @Specialization
  public boolean println(boolean value, @CachedContext(YattaLanguage.class) Context context) {
    doPrint(context.getOutput(), value);
    return value;
  }

  @TruffleBoundary
  private void doPrint(PrintWriter out, boolean value) {
    out.println(value);
  }

  @Specialization
  public String println(String value, @CachedContext(YattaLanguage.class) Context context) {
    doPrint(context.getOutput(), value);
    return value;
  }

  @TruffleBoundary
  private void doPrint(PrintWriter out, String value) {
    out.println(value);
  }

  @Specialization
  public Object println(Promise value, @CachedContext(YattaLanguage.class) Context context) {
    return value.map(val -> {
      doPrint(context.getOutput(), val);
      return val;
    }, this);
  }

  @TruffleBoundary
  private void doPrint(PrintWriter out, Promise value) {
    out.println(value);
  }

  @Specialization
  public Object println(Seq value, @CachedContext(YattaLanguage.class) Context context) {
    doPrint(context.getOutput(), value);
    return value;
  }

  @TruffleBoundary
  private void doPrint(PrintWriter out, Seq value) {
    try {
      out.println(value.asJavaString(this));
    } catch (BadArgException e) {
      out.println(value.toString());
    }
  }

  @Specialization
  public Object println(NativeObject value, @CachedContext(YattaLanguage.class) Context context) {
    doPrint(context.getOutput(), value);
    return value;
  }

  @TruffleBoundary
  private void doPrint(PrintWriter out, NativeObject value) {
    out.println(value.getValue());
  }

  @Specialization
  public Object println(Object value, @CachedContext(YattaLanguage.class) Context context) {
    doPrint(context.getOutput(), value);
    return value;
  }

  @TruffleBoundary
  private void doPrint(PrintWriter out, Object value) {
    out.println(value);
  }
}
