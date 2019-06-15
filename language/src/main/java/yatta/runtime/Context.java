package yatta.runtime;

import yatta.YattaLanguage;
import yatta.ast.builtin.*;
import yatta.ast.builtin.modules.BuiltinModuleInfo;
import yatta.ast.builtin.modules.SequenceBuiltinModule;
import yatta.runtime.async.AsyncSelectorThread;
import com.oracle.truffle.api.CallTarget;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.instrumentation.AllocationReporter;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.object.Layout;
import com.oracle.truffle.api.source.Source;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class Context {
  private static final Source BUILTIN_SOURCE = Source.newBuilder(YattaLanguage.ID, "", "yatta builtin").build();
  static final Layout LAYOUT = Layout.createLayout();

  private final TruffleLanguage.Env env;
  private final BufferedReader input;
  private final PrintWriter output;
  private final YattaLanguage language;
  private final AllocationReporter allocationReporter;
  private final Builtins builtins;
  private final BuiltinModules builtinModules;
  private final ExecutorService executor = Executors.newFixedThreadPool(4);
  private final AsyncSelectorThread asyncSelectorThread = new AsyncSelectorThread();

  public Context(YattaLanguage language, TruffleLanguage.Env env, List<NodeFactory<? extends BuiltinNode>> externalBuiltins) {
    this.env = env;
    this.input = new BufferedReader(new InputStreamReader(env.in()));
    this.output = new PrintWriter(env.out(), true);
    this.language = language;
    this.allocationReporter = env.lookup(AllocationReporter.class);
    this.builtins = new Builtins();
    this.builtinModules = new BuiltinModules();
    this.asyncSelectorThread.start();

    installBuiltins(externalBuiltins);
    installBuiltinModules();
  }

  private void installBuiltins(List<NodeFactory<? extends BuiltinNode>> externalBuiltins) {
    for (NodeFactory<? extends BuiltinNode> externalBuiltin : externalBuiltins) {
      this.builtins.register(externalBuiltin);
    }

    this.builtins.register(PrintlnBuiltinFactory.getInstance());
    this.builtins.register(SleepNodeFactory.getInstance());
    this.builtins.register(FileOpenNodeFactory.getInstance());
    this.builtins.register(FileReadLineNodeFactory.getInstance());
    this.builtins.register(AsyncNodeFactory.getInstance());
  }

  private void installBuiltinModules() {
    this.builtinModules.register(new SequenceBuiltinModule());
  }

  /**
   * Returns the default input. To allow unit
   * testing, we do not use {@link System#in} directly.
   */
  public BufferedReader getInput() {
    return input;
  }

  /**
   * The default output. To allow unit
   * testing, we do not use {@link System#out} directly.
   */
  public PrintWriter getOutput() {
    return output;
  }

  public static NodeInfo lookupNodeInfo(Class<?> clazz) {
    if (clazz == null) {
      return null;
    }
    NodeInfo info = clazz.getAnnotation(NodeInfo.class);
    if (info != null) {
      return info;
    } else {
      return lookupNodeInfo(clazz.getSuperclass());
    }
  }

  public static BuiltinModuleInfo lookupBuiltinModuleInfo(Class<?> clazz) {
    if (clazz == null) {
      return null;
    }
    BuiltinModuleInfo info = clazz.getAnnotation(BuiltinModuleInfo.class);
    if (info != null) {
      return info;
    } else {
      return lookupBuiltinModuleInfo(clazz.getSuperclass());
    }
  }

  /*
   * Methods for language interoperability.
   */

  public static Object fromForeignValue(Object a) {
    if (a instanceof Long || a instanceof String || a instanceof Boolean) {
      return a;
    } else if (a instanceof Character) {
      return String.valueOf(a);
    } else if (a instanceof Number) {
      return fromForeignNumber(a);
    } else if (a instanceof TruffleObject) {
      return a;
    } else if (a instanceof Context) {
      return a;
    }
    CompilerDirectives.transferToInterpreter();
    throw new IllegalStateException(a + " is not a Truffle value");
  }

  @CompilerDirectives.TruffleBoundary
  private static long fromForeignNumber(Object a) {
    return ((Number) a).longValue();
  }

  public CallTarget parse(Source source) {
    return env.parse(source);
  }

  public TruffleObject getPolyglotBindings() {
    return (TruffleObject) env.getPolyglotBindings();
  }

  public static Context getCurrent() {
    return YattaLanguage.getCurrentContext();
  }

  public Builtins getBuiltins() {
    return builtins;
  }

  public BuiltinModules getBuiltinModules() {
    return builtinModules;
  }

  public ExecutorService getExecutor() {
    return executor;
  }
}
