package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Context;
import yona.runtime.NativeObject;
import yona.runtime.Seq;
import yona.runtime.Unit;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

import java.io.IOException;
import java.io.PrintWriter;

@BuiltinModuleInfo(moduleName = "IO")
public final class IOBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "print")
  abstract static class PrintBuiltin extends BuiltinNode {
    @Specialization
    public long print(int value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, int value) {
      out.print(Character.toChars(value));
    }

    @Specialization
    public long print(long value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, long value) {
      out.print(value);
    }

    @Specialization
    public boolean print(boolean value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, boolean value) {
      out.print(value);
    }

    @Specialization
    public String print(String value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, String value) {
      out.print(value);
    }

    @Specialization
    public Object print(Promise value, @CachedContext(YonaLanguage.class) Context context) {
      return value.map(val -> {
        doPrint(context.getOutput(), val);
        return val;
      }, this);
    }

    @Specialization
    public Object print(Seq value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, Seq value) {
      try {
        out.print(value.asJavaString(this));
      } catch (BadArgException e) {
        out.print(value.toString());
      }
    }

    @Specialization
    public Object print(NativeObject<?> value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, NativeObject<?> value) {
      out.print(value.getValue());
    }

    @Specialization
    public Object print(Object value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, Object value) {
      out.print(value);
    }
  }

  @NodeInfo(shortName = "print_err")
  abstract static class PrinterrBuiltin extends BuiltinNode {
    @Specialization
    public long print(int value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getError(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, int value) {
      out.print(Character.toChars(value));
    }

    @Specialization
    public long print(long value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getError(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, long value) {
      out.print(value);
    }

    @Specialization
    public boolean print(boolean value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getError(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, boolean value) {
      out.print(value);
    }

    @Specialization
    public String print(String value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getError(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, String value) {
      out.print(value);
    }

    @Specialization
    public Object print(Promise value, @CachedContext(YonaLanguage.class) Context context) {
      return value.map(val -> {
        doPrint(context.getError(), val);
        return val;
      }, this);
    }

    @Specialization
    public Object print(Seq value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getError(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, Seq value) {
      try {
        out.print(value.asJavaString(this));
      } catch (BadArgException e) {
        out.print(value.toString());
      }
    }

    @Specialization
    public Object print(NativeObject<?> value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getError(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, NativeObject<?> value) {
      out.print(value.getValue());
    }

    @Specialization
    public Object print(Object value, @CachedContext(YonaLanguage.class) Context context) {
      doPrint(context.getError(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, Object value) {
      out.print(value);
    }
  }

  @NodeInfo(shortName = "read")
  abstract static class ReadBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise read(@CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Promise promise = new Promise(dispatch);
      context.ioExecutor.submit(() -> {
        try {
          promise.fulfil(context.getInput().read(), this);
        } catch (IOException e) {
          promise.fulfil(new yona.runtime.exceptions.IOException(e, this), this);
        }
      });
      return promise;
    }
  }

  @NodeInfo(shortName = "readln")
  abstract static class ReadlnBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise readln(@CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Promise promise = new Promise(dispatch);
      context.ioExecutor.submit(() -> {
        try {
          promise.fulfil(Seq.fromCharSequence(context.getInput().readLine()), this);
        } catch (IOException e) {
          promise.fulfil(new yona.runtime.exceptions.IOException(e, this), this);
        }
      });
      return promise;
    }
  }

  @NodeInfo(shortName = "flush")
  abstract static class FlushBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit flush(@CachedContext(YonaLanguage.class) Context context) {
      context.getOutput().flush();
      return Unit.INSTANCE;
    }
  }

  @NodeInfo(shortName = "flush_err")
  abstract static class FlusherrBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit flush(@CachedContext(YonaLanguage.class) Context context) {
      context.getError().flush();
      return Unit.INSTANCE;
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(IOBuiltinModuleFactory.PrintBuiltinFactory.getInstance()),
        new ExportedFunction(IOBuiltinModuleFactory.PrinterrBuiltinFactory.getInstance()),
        new ExportedFunction(IOBuiltinModuleFactory.ReadBuiltinFactory.getInstance()),
        new ExportedFunction(IOBuiltinModuleFactory.ReadlnBuiltinFactory.getInstance()),
        new ExportedFunction(IOBuiltinModuleFactory.FlushBuiltinFactory.getInstance()),
        new ExportedFunction(IOBuiltinModuleFactory.FlusherrBuiltinFactory.getInstance())
    );
  }
}
