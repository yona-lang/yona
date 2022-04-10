package yona.runtime;

import com.oracle.truffle.api.*;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.instrumentation.AllocationReporter;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.ExpressionNode;
import yona.ast.FunctionRootNode;
import yona.ast.JavaMethodRootNode;
import yona.ast.builtin.*;
import yona.ast.builtin.modules.*;
import yona.ast.builtin.modules.http.HttpClientBuiltinModule;
import yona.ast.builtin.modules.http.HttpServerBuiltinModule;
import yona.ast.builtin.modules.socket.SocketClientBuiltinModule;
import yona.ast.builtin.modules.socket.SocketConnectionBuiltinModule;
import yona.ast.builtin.modules.socket.SocketServerBuiltinModule;
import yona.ast.call.BuiltinCallNode;
import yona.ast.call.FunctionInvokeNode;
import yona.ast.call.InvokeNode;
import yona.ast.controlflow.YonaBlockNode;
import yona.ast.expression.value.AnyValueNode;
import yona.ast.local.ReadArgumentNode;
import yona.ast.local.WriteLocalVariableNode;
import yona.ast.local.WriteLocalVariableNodeGen;
import yona.runtime.annotations.ExceptionSymbol;
import yona.runtime.stdlib.BuiltinModules;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;
import yona.runtime.threading.Threading;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.nio.channels.Selector;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;

public final class Context {
  public static final Source JAVA_BUILTIN_SOURCE = Source.newBuilder("java", "", "Java builtin").internal(true).build();
  public static final SourceSection JAVA_SOURCE_SECTION = JAVA_BUILTIN_SOURCE.createUnavailableSection();
  private final TruffleLogger LOGGER = YonaLanguage.getLogger(Context.class);
  public static final String YONA_PATH = "YONA_PATH";
  private static final TruffleLanguage.ContextReference<Context> REFERENCE = TruffleLanguage.ContextReference.create(YonaLanguage.class);

  public static Context get(Node node) {
    return REFERENCE.get(node);
  }

  /**
   * cached instance of commonly used functions since they are used commonly across the board
   */
  public Function identityFunction;
  public Function generatorMapTransducer;
  public Function generatorFinalShapeReducer_seq;
  public Function generatorFinalShapeReducer_set;
  public Function generatorFinalShapeReducer_dict;
  public Function generatorFilterTransducer;
  public Function generatorReduceFunction;

  private final TruffleLanguage.Env env;
  private final BufferedReader input;
  private final PrintWriter output;
  private final PrintWriter error;
  private final YonaLanguage language;
  private final AllocationReporter allocationReporter;  // TODO use this
  public final Builtins builtins;
  public final BuiltinModules builtinModules;
  private Dict symbols = Dict.empty(Murmur3.INSTANCE, 0L);
  private Dict moduleCache = Dict.empty(Murmur3.INSTANCE, 0L);
  public Threading threading;
  public ExecutorService ioExecutor;
  public ScheduledExecutorService schedulerExecutor;
  public Dict globals = Dict.empty(Murmur3.INSTANCE, 0L);
  public final FrameDescriptor globalFrameDescriptor;
  public final MaterializedFrame globalFrame;
  private final Path stdlibHome;
  private final Path languageHome;
  private final Path cwdPath;
  private final boolean printAllResults;
  public Selector socketSelector;

  public Context(final YonaLanguage language, final TruffleLanguage.Env env, final Path languageHomePath, final Path stdlibHomePath, final Path cwdPath) {
    this.env = env;
    this.input = new BufferedReader(new InputStreamReader(env.in()));
    this.output = new PrintWriter(env.out(), true, StandardCharsets.UTF_8);
    this.error = new PrintWriter(env.err(), true, StandardCharsets.UTF_8);
    this.language = language;
    this.allocationReporter = env.lookup(AllocationReporter.class);
    this.builtins = new Builtins();
    this.builtinModules = new BuiltinModules();
    this.globalFrameDescriptor = new FrameDescriptor(UninitializedFrameSlot.INSTANCE);
    this.globalFrame = this.initGlobalFrame();
    this.languageHome = languageHomePath;
    this.stdlibHome = stdlibHomePath;
    this.cwdPath = cwdPath;
    if (env.getEnvironment().containsKey("YONA_PRINT_ALL_RESULTS")) {
      this.printAllResults = Boolean.parseBoolean(env.getEnvironment().get("YONA_PRINT_ALL_RESULTS"));
    } else {
      this.printAllResults = false;
    }
  }

  public void initialize() throws Exception {
//    LOGGER = YonaLanguage.getLogger(Context.class);
//
//    LOGGER.config("Yona Context initializing");
//    LOGGER.config("Yona language home: " + stdlibHome);

    if (languageHome == null) {
      throw new IOException("Unable to locate language home. Please set up JAVA_HOME environment variable to point to the GraalVM root folder.");
    }

    if (stdlibHome == null) {
      throw new IOException("Unable to locate language home. Please set up YONA_STDLIB_HOME environment variable to point to the GraalVM root folder.");
    }

//    LOGGER.fine("Initializing threading");

    installBuiltins();
    installBuiltinModules();
    registerBuiltins();
    installGlobals();

    this.socketSelector = Selector.open();

    this.ioExecutor = Executors.newCachedThreadPool(runnable -> env.createThread(runnable, null, new ThreadGroup("yona-io")));
    this.schedulerExecutor = Executors.newScheduledThreadPool(1, runnable -> env.createThread(runnable, null, new ThreadGroup("yona-scheduler")));
    this.threading = new Threading(language, this);
    threading.initialize();

    identityFunction = lookupGlobalFunction(null, "identity");
    generatorMapTransducer = lookupGlobalFunction("Transducers", "map");
    generatorFinalShapeReducer_seq = lookupGlobalFunction("Reducers", GeneratedCollection.SEQ.reducerForGeneratedCollection());
    generatorFinalShapeReducer_set = lookupGlobalFunction("Reducers", GeneratedCollection.SET.reducerForGeneratedCollection());
    generatorFinalShapeReducer_dict = lookupGlobalFunction("Reducers", GeneratedCollection.DICT.reducerForGeneratedCollection());
    generatorFilterTransducer = lookupGlobalFunction("Transducers", "filter");
    generatorReduceFunction = lookupGlobalFunction("Reducers", "reduce");

//    LOGGER.config("Yona Context initialized");
  }

  private MaterializedFrame initGlobalFrame() {
    VirtualFrame frame = Truffle.getRuntime().createVirtualFrame(null, this.globalFrameDescriptor);
    return frame.materialize();
  }

  private void installBuiltins() {
    builtins.register(new ExportedFunction(SleepBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(AsyncBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(IdentityBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ToStringBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ToFloatBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(ToIntegerBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(EvalBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(NeverBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(InfiBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TimesBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(DropBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(OrdBuiltinFactory.getInstance()));
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
    builtinModules.register(new IOBuiltinModule());
    builtinModules.register(new FileBuiltinModule());
    builtinModules.register(new TransducersBuiltinModule());
    builtinModules.register(new JSONBuiltinModule());
    builtinModules.register(new TupleBuiltinModule());
    builtinModules.register(new HttpClientBuiltinModule());
    builtinModules.register(new HttpServerBuiltinModule());
    builtinModules.register(new JavaBuiltinModule());
    builtinModules.register(new JavaTypesBuiltinModule());
    builtinModules.register(new SystemBuiltinModule());
    builtinModules.register(new STMBuiltinModule());
    builtinModules.register(new LocalContextBuiltinModule());
    builtinModules.register(new RegexpBuiltinModule());
    builtinModules.register(new ReflectionBuiltinModule());
    builtinModules.register(new SocketServerBuiltinModule());
    builtinModules.register(new SocketClientBuiltinModule());
    builtinModules.register(new SocketConnectionBuiltinModule());
    builtinModules.register(new StopWatchBuiltinModule());
    builtinModules.register(new TimeBuiltinModule());
    builtinModules.register(new SchedulerBuiltinModule());
    builtinModules.register(new ExceptionBuiltinModule());
    builtinModules.register(new RandomBuiltinModule());
  }

  public void installBuiltinsGlobals(String fqn, Builtins builtins) {
    final java.util.Set<String> exports = new HashSet<>(builtins.builtins.size());
    final List<Function> functions = new ArrayList<>(builtins.builtins.size());

    builtins.builtins.forEach((name, stdLibFunction) -> {
      int argumentsCount = stdLibFunction.node.getExecutionSignature().size();
      FunctionRootNode rootNode = new FunctionRootNode(language, globalFrameDescriptor, new BuiltinCallNode(stdLibFunction.node), stdLibFunction.sourceSection(), fqn, name);
      if (stdLibFunction.isExported()) {
        exports.add(name);
      }
      functions.add(new Function(fqn, name, Truffle.getRuntime().createCallTarget(rootNode), argumentsCount, stdLibFunction.unwrapArgumentPromises()));
    });

    YonaModule module = new YonaModule(fqn, exports, functions, Dict.EMPTY);
    insertGlobal(fqn, module);
  }

  private void registerBuiltins() {
    builtins.builtins.forEach((name, stdLibFunction) -> {
      int cardinality = stdLibFunction.node.getExecutionSignature().size();

      FunctionRootNode rootNode = new FunctionRootNode(language, globalFrameDescriptor, new BuiltinCallNode(stdLibFunction.node), stdLibFunction.sourceSection(), null, name);
      Function function = new Function(null, name, Truffle.getRuntime().createCallTarget(rootNode), cardinality, stdLibFunction.unwrapArgumentPromises());

      String partiallyAppliedFunctionName = "$partial-0/%d-%s".formatted(function.getCardinality(), function.getName());
      ExpressionNode[] allArgumentNodes = new ExpressionNode[function.getCardinality()];

      for (int i = 0; i < function.getCardinality(); i++) {
        allArgumentNodes[i] = new ReadArgumentNode(i);
      }

      /*
       * Partially applied function will just invoke the original function with arguments constructed as a combination
       * of those which were provided when this closure was created and those to be read on the following application
       */
      InvokeNode invokeNode = new FunctionInvokeNode(language, function, allArgumentNodes, null);

      FrameDescriptor partialFrameDescriptor = new FrameDescriptor(UninitializedFrameSlot.INSTANCE);
      /*
       * We need to make sure that the original function is still accessible within the closure, even if the partially
       * applied function already leaves the scope with the original function
       */
      WriteLocalVariableNode writeLocalVariableNode = WriteLocalVariableNodeGen.create(new AnyValueNode(function), partialFrameDescriptor.addFrameSlot(function.getName()));

      YonaBlockNode blockNode = new YonaBlockNode(new ExpressionNode[]{writeLocalVariableNode, invokeNode});
      FunctionRootNode partiallyAppliedFunctionRootNode = new FunctionRootNode(language, partialFrameDescriptor, blockNode, stdLibFunction.sourceSection(), null, partiallyAppliedFunctionName);

      insertGlobal(name, new Function(null, partiallyAppliedFunctionName, Truffle.getRuntime().createCallTarget(partiallyAppliedFunctionRootNode), cardinality, stdLibFunction.unwrapArgumentPromises()));
    });
  }

  private void installGlobals() {
    builtinModules.builtins.forEach(this::installBuiltinsGlobals);

//    LOGGER.config("Installing globals from: " + stdlibHome);
    try {
      env.getInternalTruffleFile(stdlibHome.toUri()).visit(new FileVisitor<>() {
        @Override
        public FileVisitResult preVisitDirectory(TruffleFile dir, BasicFileAttributes attrs) {
          return FileVisitResult.CONTINUE;
        }

        @Override
        public FileVisitResult visitFile(TruffleFile file, BasicFileAttributes attrs) {
          String relativePath = stdlibHome.toUri().relativize(file.toUri()).getPath();
          if (relativePath.endsWith("." + YonaLanguage.ID)) {
            String moduleFQN = relativePath.substring(0, relativePath.lastIndexOf(".")).replaceAll("/", "\\\\");
//            LOGGER.config("Loading stdlib module: " + moduleFQN);
            try {
              YonaModule module = loadStdModule(file, moduleFQN);
              insertGlobal(moduleFQN, module);
            } catch (IOException e) {
//              LOGGER.config(e.getMessage());
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

  /**
   * The default error output. To allow unit
   * testing, we do not use {@link System#out} directly.
   */
  public PrintWriter getError() {
    return error;
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
  public void cacheModule(String FQN, YonaModule module) {
    moduleCache = moduleCache.add(FQN, module);
  }

  @CompilerDirectives.TruffleBoundary
  public YonaModule lookupModule(String[] packageParts, String moduleName, Node node) {
    String FQN = getFQN(packageParts, moduleName);
    Object module = moduleCache.lookup(FQN);
    if (module == Unit.INSTANCE) {
      module = loadModule(packageParts, moduleName, FQN, node);
      moduleCache = moduleCache.add(FQN, module);
    }

    return (YonaModule) module;
  }

  @CompilerDirectives.TruffleBoundary
  private String[] getYonaPath() {
    if (env.getEnvironment().containsKey(YONA_PATH)) {
      return env.getEnvironment().get(YONA_PATH).split(env.getPathSeparator());
    } else {
      return new String[]{env.getCurrentWorkingDirectory().toString()};
    }
  }

  @CompilerDirectives.TruffleBoundary
  private YonaModule loadModule(String[] packageParts, String moduleName, String FQN, Node node) {
    YonaModule javaModule = loadJavaModule(FQN);
    if (javaModule != null) {
      return javaModule;
    } else {
      Path path = pathForModule(packageParts, moduleName);
      String[] yonaPaths = getYonaPath();
      for (String yonaPath : yonaPaths) {
        Path fullPath = Paths.get(yonaPath, path.toString());
        if (Files.exists(fullPath)) {
          YonaModule module = loadModule(env.getPublicTruffleFile(fullPath.toUri()), FQN, node, true);
          if (module != null) {
            return module;
          }
        }
      }
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new YonaException("Module %s not found in YONA_PATH: %s".formatted(FQN, Arrays.toString(yonaPaths)), node);
    }
  }

  @CompilerDirectives.TruffleBoundary
  private YonaModule loadStdModule(TruffleFile file, String FQN) throws IOException {
    YonaModule module = loadModule(file, FQN, null, false);
    if (module != null) {
      return module;
    } else {
      throw new IOException("StdLib Module %s not found: ".formatted(FQN));
    }
  }

  @CompilerDirectives.TruffleBoundary
  private Path pathForModule(String[] packageParts, String moduleName) {
    Path path;
    if (packageParts.length > 0) {
      String[] pathParts = new String[packageParts.length];
      System.arraycopy(packageParts, 1, pathParts, 0, packageParts.length - 1);
      pathParts[pathParts.length - 1] = moduleName + "." + YonaLanguage.ID;
      path = Paths.get(packageParts[0], pathParts);
    } else {
      path = Paths.get("%s.%s".formatted(moduleName, YonaLanguage.ID));
    }

    return path;
  }

  @CompilerDirectives.TruffleBoundary
  private YonaModule loadModule(TruffleFile file, String FQN, Node node, boolean cache) {
    try {
      Source source = Source.newBuilder(YonaLanguage.ID, file).build();
      CallTarget callTarget = env.parseInternal(source);
      YonaModule module = (YonaModule) callTarget.call();

      if (!FQN.equals(module.getFqn())) {
        Context context = Context.get(node);
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new YonaException("Module file %s has incorrectly defined module as %s".formatted(context.cwdPath.relativize(Paths.get(file.toRelativeUri())).toString().replace('\\', '/'), module.getFqn()), node);
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
  private YonaModule loadJavaModule(String FQN) {
    try {
      Class<?> cls = Class.forName(FQN.replace("\\", "."));
      Method[] methods = cls.getMethods();
      List<Function> functions = new ArrayList<>(methods.length);
      java.util.Set<String> exports = new HashSet<>(methods.length);

      for (Method method : methods) {
        exports.add(method.getName());
        Function javaFunction = JavaMethodRootNode.buildFunction(language, method, globalFrameDescriptor.copy(), null);
        functions.add(javaFunction);
      }

      YonaModule module = new YonaModule(FQN, exports, functions, Dict.EMPTY);
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

  public void insertGlobal(String fqn, YonaModule module) {
    Object existingObject = globals.lookup(fqn);
    if (Unit.INSTANCE == existingObject) {
      globals = globals.add(fqn, module);
    } else {
      YonaModule existingModule = (YonaModule) existingObject;
      globals = globals.add(fqn, existingModule.merge(module));
    }
  }

  public Function lookupGlobalFunction(String fqn, String function) {
    if (fqn != null && globals.contains(fqn)) {
      YonaModule yonaModule = (YonaModule) globals.lookup(fqn);
      return yonaModule.getFunctions().get(function);
    } else if (fqn == null) {
      return (Function) globals.lookup(function);
    }
    return null;
  }

  @CompilerDirectives.TruffleBoundary
  public void dispose() {
//    LOGGER.fine("Threading shutting down");

    threading.dispose();
    ioExecutor.shutdown();
    assert ioExecutor.shutdownNow().isEmpty();
    assert ioExecutor.isShutdown();

    schedulerExecutor.shutdown();
    assert schedulerExecutor.shutdownNow().isEmpty();
    assert schedulerExecutor.isShutdown();

    //    LOGGER.fine("Threading shut down");

    output.flush();
    error.flush();

    while (!ioExecutor.isTerminated() || !schedulerExecutor.isTerminated()) {
      try {
        Thread.sleep(10);
      } catch (InterruptedException ignored) {
      }
    }
  }

  @CompilerDirectives.TruffleBoundary
  public Set globallyProvidedIdentifiers() {
    return Set.set(builtins.builtins.keySet().toArray());
  }

  @CompilerDirectives.TruffleBoundary
  public Seq languageHome() {
    return Seq.fromCharSequence(languageHome.toFile().getAbsolutePath());
  }

  public boolean isPrintAllResults() {
    return printAllResults;
  }

  public AllocationReporter getAllocationReporter() {
    return allocationReporter;
  }
}
