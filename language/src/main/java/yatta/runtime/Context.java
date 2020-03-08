package yatta.runtime;

import com.oracle.truffle.api.*;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.instrumentation.AllocationReporter;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.FunctionRootNode;
import yatta.ast.builtin.*;
import yatta.ast.builtin.modules.*;
import yatta.ast.call.BuiltinCallNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.controlflow.YattaBlockNode;
import yatta.ast.expression.SimpleIdentifierNode;
import yatta.ast.expression.value.AnyValueNode;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.local.WriteLocalVariableNode;
import yatta.ast.local.WriteLocalVariableNodeGen;
import yatta.runtime.annotations.ExceptionSymbol;
import yatta.runtime.stdlib.BuiltinModules;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;
import yatta.runtime.threading.Threading;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.nio.file.FileVisitResult;
import java.nio.file.FileVisitor;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class Context {
  public static final Source BUILTIN_SOURCE = Source.newBuilder(YattaLanguage.ID, "", "Yatta builtin").build();
  private static final SourceSection BUILTIN_SOURCE_SECTION = BUILTIN_SOURCE.createUnavailableSection();
  private static final String STDLIB_FOLDER = "lib-yatta";
  private static final int STDLIB_PREFIX_LENGTH = STDLIB_FOLDER.length() + 1;  // "lib-yatta".length() + 1
  private static final int LANGUAGE_ID_SUFFIX_LENGTH = YattaLanguage.ID.length() + 1;  // ".yatta".length()

  private final TruffleLanguage.Env env;
  private final BufferedReader input;
  private final PrintWriter output;
  private final YattaLanguage language;
  private final AllocationReporter allocationReporter;  // TODO use this
  public final Builtins builtins;
  public final BuiltinModules builtinModules;
  private Dict symbols = Dict.empty(Murmur3.INSTANCE, 0L);
  private Dict moduleCache = Dict.empty(Murmur3.INSTANCE, 0L);
  public final Threading threading;
  public final ExecutorService ioExecutor;
  public Dict globals = Dict.empty(Murmur3.INSTANCE, 0L);

  public Context(YattaLanguage language, TruffleLanguage.Env env) {
    this.env = env;
    this.input = new BufferedReader(new InputStreamReader(env.in()));
    this.output = new PrintWriter(env.out(), true);
    this.language = language;
    this.allocationReporter = env.lookup(AllocationReporter.class);
    this.builtins = new Builtins();
    this.builtinModules = new BuiltinModules();
    this.ioExecutor = Executors.newCachedThreadPool(runnable -> env.createThread(runnable, null, new ThreadGroup("yatta-io")));
    this.threading = new Threading(env);
  }

  public void initialize() {
    threading.initialize();

    installBuiltins();
    installBuiltinModules();
    registerBuiltins();
    installGlobals();
  }

  private void installBuiltins() {
    builtins.register(new ExportedFunction(PrintlnBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SleepBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(AsyncBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(IdentityBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ToStringBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SystemBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(EvalBuiltinFactory.getInstance()));
  }

  private void installBuiltinModules() {
    builtinModules.register(new TypesBuiltinModule());
    builtinModules.register(new SeqBuiltinModule());
    builtinModules.register(new SetBuiltinModule());
    builtinModules.register(new DictBuiltinModule());
    builtinModules.register(new FileBuiltinModule());
    builtinModules.register(new TransducersBuiltinModule());
    builtinModules.register(new TimeBuiltinModule());
  }

  public void installBuiltinsGlobals(String fqn, Builtins builtins) {
    final List<String> exports = new ArrayList<>(builtins.builtins.size());
    final List<Function> functions = new ArrayList<>(builtins.builtins.size());

    builtins.builtins.forEach((name, stdLibFunction) -> {
      int argumentsCount = stdLibFunction.node.getExecutionSignature().size();
      FunctionRootNode rootNode = new FunctionRootNode(language, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), new BuiltinCallNode(stdLibFunction.node), BUILTIN_SOURCE_SECTION, name);
      if (stdLibFunction.isExported()) {
        exports.add(name);
      }
      functions.add(new Function(name, Truffle.getRuntime().createCallTarget(rootNode), argumentsCount));
    });

    YattaModule module = new YattaModule(fqn, exports, functions, Dict.empty());
    insertGlobal(fqn, module);
  }

  private void registerBuiltins() {
    builtins.builtins.forEach((name, stdLibFunction) -> {
      int cardinality = stdLibFunction.node.getExecutionSignature().size();

      FunctionRootNode rootNode = new FunctionRootNode(language, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), new BuiltinCallNode(stdLibFunction.node), BUILTIN_SOURCE_SECTION, name);
      Function function = new Function(name, Truffle.getRuntime().createCallTarget(rootNode), cardinality);

      String partiallyAppliedFunctionName = "$partial-0/" + function.getCardinality() + "-" + function.getName();
      ExpressionNode[] allArgumentNodes = new ExpressionNode[function.getCardinality()];

      for (int i = 0; i < function.getCardinality(); i++) {
        allArgumentNodes[i] = new ReadArgumentNode(i);
      }

      /*
       * Partially applied function will just invoke the original function with arguments constructed as a combination
       * of those which were provided when this closure was created and those to be read on the following application
       */
      InvokeNode invokeNode = new InvokeNode(language, new SimpleIdentifierNode(function.getName()), allArgumentNodes, null);

      FrameDescriptor partialFrameDescriptor = new FrameDescriptor(UninitializedFrameSlot.INSTANCE);
      /*
       * We need to make sure that the original function is still accessible within the closure, even if the partially
       * applied function already leaves the scope with the original function
       */
      WriteLocalVariableNode writeLocalVariableNode = WriteLocalVariableNodeGen.create(new AnyValueNode(function), partialFrameDescriptor.addFrameSlot(function.getName()));

      YattaBlockNode blockNode = new YattaBlockNode(new ExpressionNode[]{writeLocalVariableNode, invokeNode});
      FunctionRootNode partiallyAppliedFunctionRootNode = new FunctionRootNode(language, partialFrameDescriptor, blockNode, BUILTIN_SOURCE_SECTION, partiallyAppliedFunctionName);

      insertGlobal(name, new Function(partiallyAppliedFunctionName, Truffle.getRuntime().createCallTarget(partiallyAppliedFunctionRootNode), cardinality));
    });
  }

  private void installGlobals() {
    builtinModules.builtins.forEach(this::installBuiltinsGlobals);

    try {
      env.getPublicTruffleFile(STDLIB_FOLDER).visit(new FileVisitor<TruffleFile>() {
        @Override
        public FileVisitResult preVisitDirectory(TruffleFile dir, BasicFileAttributes attrs) throws IOException {
          return FileVisitResult.CONTINUE;
        }

        @Override
        public FileVisitResult visitFile(TruffleFile file, BasicFileAttributes attrs) throws IOException {
          String relativePath = file.toRelativeUri().toString();
          String moduleFQN = relativePath.substring(STDLIB_PREFIX_LENGTH, relativePath.length() - LANGUAGE_ID_SUFFIX_LENGTH).replaceAll("/", "\\\\");
          YattaModule module = loadStdModule(file, moduleFQN);
          insertGlobal(moduleFQN, module);
          return FileVisitResult.CONTINUE;
        }

        @Override
        public FileVisitResult visitFileFailed(TruffleFile file, IOException exc) throws IOException {
          return FileVisitResult.CONTINUE;
        }

        @Override
        public FileVisitResult postVisitDirectory(TruffleFile dir, IOException exc) throws IOException {
          return FileVisitResult.CONTINUE;
        }
      }, 10);
    } catch (IOException e) {
      e.printStackTrace();
    }
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
  public void cacheModule(String FQN, YattaModule module) {
    moduleCache = moduleCache.add(FQN, module);
  }

  @CompilerDirectives.TruffleBoundary
  public YattaModule lookupModule(String[] packageParts, String moduleName, Node node) {
    String FQN = getFQN(packageParts, moduleName);
    Object module = moduleCache.lookup(FQN);
    if (module == Unit.INSTANCE) {
      module = loadModule(packageParts, moduleName, FQN, node);
      moduleCache = moduleCache.add(FQN, module);
    }

    return (YattaModule) module;
  }

  @CompilerDirectives.TruffleBoundary
  private YattaModule loadModule(String[] packageParts, String moduleName, String FQN, Node node) {
    Path path = pathForModule(packageParts, moduleName);
    return loadModule(env.getPublicTruffleFile(path.toUri()), FQN, node, true);
  }

  @CompilerDirectives.TruffleBoundary
  private YattaModule loadStdModule(TruffleFile file, String FQN) {
    return loadModule(file, FQN, null, false);
  }

  @CompilerDirectives.TruffleBoundary
  private Path pathForModule(String[] packageParts, String moduleName) {
    Path path;
    if (packageParts.length > 0) {
      String[] pathParts = new String[packageParts.length];
      System.arraycopy(packageParts, 1, pathParts, 0, packageParts.length - 1);
      pathParts[pathParts.length - 1] = moduleName + "." + YattaLanguage.ID;
      path = Paths.get(packageParts[0], pathParts);
    } else {
      path = Paths.get(moduleName + "." + YattaLanguage.ID);
    }

    return path;
  }

  @CompilerDirectives.TruffleBoundary
  private YattaModule loadModule(TruffleFile file, String FQN, Node node, boolean cache) {
    try {
      Source source = Source.newBuilder(YattaLanguage.ID, file).build();
      CallTarget callTarget = env.parseInternal(source);
      YattaModule module = (YattaModule) callTarget.call();

      if (!FQN.equals(module.getFqn())) {
        throw new YattaException("Module file " + file.getPath().substring(Paths.get(".").toUri().toURL().getFile().length() - 2) + " has incorrectly defined module as " + module.getFqn(), node);
      }
      if (cache) {
        moduleCache = this.moduleCache.add(FQN, module);
      }

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

  public TruffleObject getPolyglotBindings() {
    return (TruffleObject) env.getPolyglotBindings();
  }

  public static Context getCurrent() {
    return YattaLanguage.getCurrentContext();
  }

  public Symbol symbol(String name) {
    Object symbol = symbols.lookup(name);
    if (symbol == Unit.INSTANCE) {
      symbol = new Symbol(name);
      symbols = symbols.add(name, symbol);
    }

    return (Symbol) symbol;
  }

  public void insertGlobal(String functionName, Function function) {
    globals = globals.add(functionName, function);
  }

  public void insertGlobal(String fqn, YattaModule module) {
    Object existingObject = globals.lookup(fqn);
    if (Unit.INSTANCE.equals(existingObject)) {
      globals = globals.add(fqn, module);
    } else {
      YattaModule existingModule = (YattaModule) existingObject;
      globals = globals.add(fqn, existingModule.merge(module));
    }
  }

  public Function lookupGlobalFunction(String fqn, String function) {
    if (globals.contains(fqn)) {
      YattaModule yattaModule = (YattaModule) globals.lookup(fqn);
      return yattaModule.getFunctions().get(function);
    }
    return null;
  }

  @CompilerDirectives.TruffleBoundary
  public void dispose() {
    threading.dispose();
    assert ioExecutor.shutdownNow().isEmpty();
  }
}
