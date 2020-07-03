package yatta.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Context;
import yatta.runtime.NativeObject;
import yatta.runtime.Seq;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

import java.io.IOException;
import java.io.PrintWriter;

@BuiltinModuleInfo(moduleName = "IO")
public final class IOBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "println")
  abstract static class PrintlnBuiltin extends BuiltinNode {
    @Specialization
    public long println(long value, @CachedContext(YattaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, long value) {
      out.println(value);
    }

    @Specialization
    public boolean println(boolean value, @CachedContext(YattaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, boolean value) {
      out.println(value);
    }

    @Specialization
    public String println(String value, @CachedContext(YattaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
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

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, Promise value) {
      out.println(value);
    }

    @Specialization
    public Object println(Seq value, @CachedContext(YattaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
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

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, NativeObject value) {
      out.println(value.getValue());
    }

    @Specialization
    public Object println(Object value, @CachedContext(YattaLanguage.class) Context context) {
      doPrint(context.getOutput(), value);
      return value;
    }

    @CompilerDirectives.TruffleBoundary
    private void doPrint(PrintWriter out, Object value) {
      out.println(value);
    }
  }

  @NodeInfo(shortName = "read")
  abstract static class ReadBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise read(@CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Promise promise = new Promise(dispatch);
      context.ioExecutor.submit(() -> {
        try {
          promise.fulfil(context.getInput().read(), this);
        } catch (IOException e) {
          promise.fulfil(new yatta.runtime.exceptions.IOException(e, this), this);
        }
      });
      return promise;
    }
  }

  @NodeInfo(shortName = "readln")
  abstract static class ReadlnBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise readln(@CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Promise promise = new Promise(dispatch);
      context.ioExecutor.submit(() -> {
        try {
          promise.fulfil(Seq.fromCharSequence(context.getInput().readLine()), this);
        } catch (IOException e) {
          promise.fulfil(new yatta.runtime.exceptions.IOException(e, this), this);
        }
      });
      return promise;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(IOBuiltinModuleFactory.PrintlnBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(IOBuiltinModuleFactory.ReadBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(IOBuiltinModuleFactory.ReadlnBuiltinFactory.getInstance()));
    return builtins;
  }
}
