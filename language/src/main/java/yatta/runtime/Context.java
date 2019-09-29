package yatta.runtime;

import com.oracle.truffle.api.CallTarget;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.instrumentation.AllocationReporter;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.builtin.*;
import yatta.ast.builtin.modules.BuiltinModuleInfo;
import yatta.ast.builtin.modules.FileBuiltinModule;
import yatta.ast.builtin.modules.SequenceBuiltinModule;
import yatta.runtime.annotations.ExceptionSymbol;
import yatta.runtime.threading.Threading;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class Context {
  private final TruffleLanguage.Env env;
  private final BufferedReader input;
  private final PrintWriter output;
  private final YattaLanguage language;
  private final AllocationReporter allocationReporter;  // TODO use this
  private final Builtins builtins;
  private final BuiltinModules builtinModules;
  private Dictionary symbols = Dictionary.dictionary();
  private Dictionary moduleCache = Dictionary.dictionary();
  public final Threading threading;
  public final ExecutorService ioExecutor;

  public Context(YattaLanguage language, TruffleLanguage.Env env, List<NodeFactory<? extends BuiltinNode>> externalBuiltins) {
    this.env = env;
    this.input = new BufferedReader(new InputStreamReader(env.in()));
    this.output = new PrintWriter(env.out(), true);
    this.language = language;
    this.allocationReporter = env.lookup(AllocationReporter.class);
    this.builtins = new Builtins();
    this.builtinModules = new BuiltinModules();
    this.threading = new Threading(env);
    this.ioExecutor = Executors.newCachedThreadPool();

    installBuiltins(externalBuiltins);
    installBuiltinModules();
  }

  private void installBuiltins(List<NodeFactory<? extends BuiltinNode>> externalBuiltins) {
    for (NodeFactory<? extends BuiltinNode> externalBuiltin : externalBuiltins) {
      this.builtins.register(externalBuiltin);
    }

    this.builtins.register(PrintlnBuiltinFactory.getInstance());
    this.builtins.register(SleepBuiltinFactory.getInstance());
    this.builtins.register(AsyncBuiltinFactory.getInstance());
    this.builtins.register(ToStringBuiltinFactory.getInstance());
    this.builtins.register(SystemBuiltinFactory.getInstance());
  }

  private void installBuiltinModules() {
    this.builtinModules.register(new SequenceBuiltinModule());
    this.builtinModules.register(new FileBuiltinModule());
  }

  public TruffleLanguage.Env getEnv() {
    return env;
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

  public Symbol lookupExceptionSymbol(Class<?> clazz) {
    if (clazz == null) {
      return null;
    }
    ExceptionSymbol info = clazz.getAnnotation(ExceptionSymbol.class);
    if (info != null) {
      return symbol(info.value());
    } else {
      return lookupExceptionSymbol(clazz.getSuperclass());
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

  @CompilerDirectives.TruffleBoundary
  public void cacheModule(String FQN, Module module) {
    moduleCache = moduleCache.insert(FQN, module);
  }

  @CompilerDirectives.TruffleBoundary
  public Module lookupModule(String[] packageParts, String moduleName, Node node) {
    String FQN = getFQN(packageParts, moduleName);
    Object module = moduleCache.lookup(FQN);
    if (module == Unit.INSTANCE) {
      module = loadModule(packageParts, moduleName, FQN, node);
      moduleCache = moduleCache.insert(FQN, module);
    }

    return (Module) module;
  }

  @CompilerDirectives.TruffleBoundary
  private Module loadModule(String[] packageParts, String moduleName, String FQN, Node node) {
    try {
      Path path;
      if (packageParts.length > 0) {
        String[] pathParts = new String[packageParts.length];
        System.arraycopy(packageParts, 1, pathParts, 0, packageParts.length - 1);
        pathParts[pathParts.length - 1] = moduleName + "." + YattaLanguage.ID;
        path = Paths.get(packageParts[0], pathParts);
      } else {
        path = Paths.get(moduleName);
      }
      URL url = path.toUri().toURL();

      Source source = Source.newBuilder(YattaLanguage.ID, url).build();
      CallTarget callTarget = parse(source);
      Module module = (Module) callTarget.call();

      if (!FQN.equals(module.getFqn())) {
        throw new YattaException("Module file " + url.getPath().substring(Paths.get(".").toUri().toURL().getFile().length() - 2) + " has incorrectly defined module as " + module.getFqn(), node);
      }
      moduleCache = this.moduleCache.insert(FQN, module);

      return module;
    } catch (IOException e) {
      throw new YattaException("Unable to load Module " + FQN + " due to: " + e.getMessage(), e, node);
    }
  }

  @CompilerDirectives.TruffleBoundary
  public static String getFQN(String[] packageParts, String moduleName) {
    if (packageParts.length > 0) {
      StringBuilder sb = new StringBuilder();
      for (String packagePart : packageParts) {
        sb.append(packagePart);
        sb.append("\\");
      }
      sb.append(moduleName);
      return sb.toString();
    } else {
      return moduleName;
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
    CompilerDirectives.transferToInterpreterAndInvalidate();
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

  public Symbol symbol(String name) {
    Object symbol = symbols.lookup(name);
    if (symbol == Unit.INSTANCE) {
      symbol = new Symbol(name);
      symbols = symbols.insert(name, symbol);
    }

    return (Symbol) symbol;
  }
}
