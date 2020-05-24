package yatta.runtime;

import com.oracle.truffle.api.*;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
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
import yatta.ast.JavaMethodRootNode;
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
import java.lang.reflect.Method;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class Context {
  public static final Source BUILTIN_SOURCE = Source.newBuilder(YattaLanguage.ID, "", "Yatta builtin").internal(true).build();
  private static final SourceSection BUILTIN_SOURCE_SECTION = BUILTIN_SOURCE.createUnavailableSection();
  public static final Source JAVA_BUILTIN_SOURCE = Source.newBuilder("java", "", "Java builtin").internal(true).build();
  public static final SourceSection JAVA_SOURCE_SECTION = JAVA_BUILTIN_SOURCE.createUnavailableSection();
  public static final Source SHUTDOWN_SOURCE = Source.newBuilder(YattaLanguage.ID, "shutdown", "shutdown").internal(true).build();
  private static final TruffleLogger LOGGER = YattaLanguage.getLogger(Context.class);
  public static final String YATTA_PATH = "YATTA_PATH";

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
  public final FrameDescriptor globalFrameDescriptor;
  public final MaterializedFrame globalFrame;
  private final Path languageHome;

  public Context(final YattaLanguage language, final TruffleLanguage.Env env, final Path languageHome) {
    this.env = env;
    this.input = new BufferedReader(new InputStreamReader(env.in()));
    this.output = new PrintWriter(env.out(), true);
    this.language = language;
    this.allocationReporter = env.lookup(AllocationReporter.class);
    this.builtins = new Builtins();
    this.builtinModules = new BuiltinModules();
    this.ioExecutor = Executors.newCachedThreadPool(runnable -> env.createThread(runnable, null, new ThreadGroup("yatta-io")));
    this.threading = new Threading(env);
    this.globalFrameDescriptor = new FrameDescriptor(UninitializedFrameSlot.INSTANCE);
    this.globalFrame = this.initGlobalFrame();
    this.languageHome = languageHome;
    LOGGER.config("Yatta language home: " + languageHome);
  }

  public void initialize() throws Exception {
    LOGGER.config("Yatta Context initializing");

    if (languageHome == null) {
      throw new IOException("Unable to locate language home. Please set up JAVA_HOME environment variable to point to the GraalVM root folder.");
    }

    LOGGER.fine("Initializing threading");
    threading.initialize();

    installBuiltins();
    installBuiltinModules();
    registerBuiltins();
    installGlobals();
    LOGGER.config("Yatta Context initialized");
  }

  private MaterializedFrame initGlobalFrame() {
    VirtualFrame frame = Truffle.getRuntime().createVirtualFrame(null, this.globalFrameDescriptor);
    return frame.materialize();
  }

  private void installBuiltins() {
    builtins.register(new ExportedFunction(PrintlnBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SleepBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(AsyncBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(IdentityBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ToStringBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ToFloatBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ToIntegerBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(EvalBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ReadlnBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ReadBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(NeverBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TimeoutBuiltinFactory.getInstance()) {
      @Override
      public boolean unwrapArgumentPromises() {
        return false;
      }
    });
  }

  private void installBuiltinModules() {
    builtinModules.register(new TypesBuiltinModule());
    builtinModules.register(new SeqBuiltinModule());
    builtinModules.register(new SetBuiltinModule());
    builtinModules.register(new DictBuiltinModule());
    builtinModules.register(new FileBuiltinModule());
    builtinModules.register(new TransducersBuiltinModule());
    builtinModules.register(new JSONBuiltinModule());
    builtinModules.register(new TupleBuiltinModule());
    builtinModules.register(new HttpClientBuiltinModule());
    builtinModules.register(new HttpServerBuiltinModule());
    builtinModules.register(new JavaBuiltinModule());
    builtinModules.register(new JavaTypesBuiltinModule());
    builtinModules.register(new SystemBuiltinModule());
    //builtinModules.register(new STMBuiltinModule());
  }

  public void installBuiltinsGlobals(String fqn, Builtins builtins) {
    final List<String> exports = new ArrayList<>(builtins.builtins.size());
    final List<Function> functions = new ArrayList<>(builtins.builtins.size());

    builtins.builtins.forEach((name, stdLibFunction) -> {
      int argumentsCount = stdLibFunction.node.getExecutionSignature().size();
      FunctionRootNode rootNode = new FunctionRootNode(language, globalFrameDescriptor, new BuiltinCallNode(stdLibFunction.node), BUILTIN_SOURCE_SECTION, fqn, name);
      if (stdLibFunction.isExported()) {
        exports.add(name);
      }
      functions.add(new Function(fqn, name, Truffle.getRuntime().createCallTarget(rootNode), argumentsCount, stdLibFunction.unwrapArgumentPromises()));
    });

    YattaModule module = new YattaModule(fqn, exports, functions, Dict.EMPTY);
    insertGlobal(fqn, module);
  }

  private void registerBuiltins() {
    builtins.builtins.forEach((name, stdLibFunction) -> {
      int cardinality = stdLibFunction.node.getExecutionSignature().size();

      FunctionRootNode rootNode = new FunctionRootNode(language, globalFrameDescriptor, new BuiltinCallNode(stdLibFunction.node), BUILTIN_SOURCE_SECTION, null, name);
      Function function = new Function(null, name, Truffle.getRuntime().createCallTarget(rootNode), cardinality, stdLibFunction.unwrapArgumentPromises());

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
      FunctionRootNode partiallyAppliedFunctionRootNode = new FunctionRootNode(language, partialFrameDescriptor, blockNode, BUILTIN_SOURCE_SECTION, null, partiallyAppliedFunctionName);

      insertGlobal(name, new Function(null, partiallyAppliedFunctionName, Truffle.getRuntime().createCallTarget(partiallyAppliedFunctionRootNode), cardinality, stdLibFunction.unwrapArgumentPromises()));
    });
  }

  private void installGlobals() {
    builtinModules.builtins.forEach(this::installBuiltinsGlobals);

    LOGGER.config("Installing globals from: " + languageHome);
    try {
      env.getInternalTruffleFile(languageHome.toUri()).visit(new FileVisitor<>() {
        @Override
        public FileVisitResult preVisitDirectory(TruffleFile dir, BasicFileAttributes attrs) {
          return FileVisitResult.CONTINUE;
        }

        @Override
        public FileVisitResult visitFile(TruffleFile file, BasicFileAttributes attrs) {
          String relativizedPath = languageHome.toUri().relativize(file.toUri()).getPath();
          if (relativizedPath.endsWith("." + YattaLanguage.ID)) {
            String moduleFQN = relativizedPath.substring(0, relativizedPath.lastIndexOf(".")).replaceAll("/", "\\\\");
            LOGGER.config("Loading stdlib module: " + moduleFQN);
            try {
              YattaModule module = loadStdModule(file, moduleFQN);
              insertGlobal(moduleFQN, module);
            } catch (IOException e) {
              LOGGER.config(e.getMessage());
            }
          }
          return FileVisitResult.CONTINUE;
        }

        @Override
        public FileVisitResult visitFileFailed(TruffleFile file, IOException exc) {
          return FileVisitResult.CONTINUE;
        }

        @Override
        public FileVisitResult postVisitDirectory(TruffleFile dir, IOException exc) {
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
  private String[] getYattaPath() {
    if (env.getEnvironment().containsKey(YATTA_PATH)) {
      return env.getEnvironment().get(YATTA_PATH).split(env.getPathSeparator());
    } else {
      return new String[]{env.getCurrentWorkingDirectory().toString()};
    }
  }

  @CompilerDirectives.TruffleBoundary
  private YattaModule loadModule(String[] packageParts, String moduleName, String FQN, Node node) {
    YattaModule javaModule = loadJavaModule(FQN);
    if (javaModule != null) {
      return javaModule;
    } else {
      Path path = pathForModule(packageParts, moduleName);
      String[] yattaPaths = getYattaPath();
      for (String yattaPath : yattaPaths) {
        Path fullPath = Paths.get(yattaPath, path.toString());
        if (Files.exists(fullPath)) {
          YattaModule module = loadModule(env.getPublicTruffleFile(fullPath.toUri()), FQN, node, true);
          if (module != null) {
            return module;
          }
        }
      }
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new YattaException("Module " + FQN + " not found in YATTA_PATH: " + Arrays.toString(yattaPaths), node);
    }
  }

  @CompilerDirectives.TruffleBoundary
  private YattaModule loadStdModule(TruffleFile file, String FQN) throws IOException {
    YattaModule module = loadModule(file, FQN, null, false);
    if (module != null) {
      return module;
    } else {
      throw new IOException("StdLib Module " + FQN + " not found: ");
    }
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
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new YattaException("Module file " + file.getPath().substring(Paths.get(".").toUri().toURL().getFile().length() - 2) + " has incorrectly defined module as " + module.getFqn(), node);
      }
      if (cache) {
        moduleCache = this.moduleCache.add(FQN, module);
      }

      return module;
    } catch (IOException e) {
      return null;
    }
  }

  @CompilerDirectives.TruffleBoundary
  private YattaModule loadJavaModule(String FQN) {
    try {
      Class cls = Class.forName(FQN.replace("\\", "."));
      Method[] methods = cls.getMethods();
      List<Function> functions = new ArrayList<>(methods.length);
      List<String> exports = new ArrayList<>(methods.length);

      for (Method method : methods) {
        exports.add(method.getName());
        Function javaFunction = JavaMethodRootNode.buildFunction(language, method, globalFrameDescriptor, null);
        functions.add(javaFunction);
      }

      YattaModule module = new YattaModule(FQN, exports, functions, Dict.EMPTY);
      moduleCache = this.moduleCache.add(FQN, module);
      return module;
    } catch (ClassNotFoundException classNotFoundException) {
      return null;
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
    if (Unit.INSTANCE == existingObject) {
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
    LOGGER.fine("Threading shutting down");
    threading.dispose();
    ioExecutor.shutdown();
    assert ioExecutor.shutdownNow().isEmpty();
    assert ioExecutor.isShutdown();
    while (!ioExecutor.isTerminated()) {
      try {
        Thread.sleep(1);
      } catch (InterruptedException e) {
        e.printStackTrace();
      }
    }
    LOGGER.fine("Threading shut down");
  }

  @CompilerDirectives.TruffleBoundary
  public Set globallyProvidedIdentifiers() {
    return Set.set(builtins.builtins.keySet().toArray());
  }
}
