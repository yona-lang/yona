package yatta.ast.builtin.modules;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.TypesGen;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Context;
import yatta.runtime.Seq;
import yatta.runtime.Tuple;
import yatta.runtime.Unit;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

@BuiltinModuleInfo(moduleName = "System")
public final class SystemBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise system(Seq sequence, @CachedContext(YattaLanguage.class) Context context) {
      Object processBuilderObj = buildProcess(sequence.unwrapPromises(this), this);

      if (processBuilderObj instanceof ProcessBuilder) {
        ProcessBuilder processBuilder = (ProcessBuilder) processBuilderObj;
        return startProcess(processBuilder, context);
      } else { // Promise
        return ((Promise) processBuilderObj).map((obj) -> startProcess((ProcessBuilder) obj, context), this);
      }
    }

    private Promise startProcess(ProcessBuilder processBuilder, Context context) {
      try {
        Process process = processBuilder.start();
        return runProcess(process, context, this);
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "pipeline")
  abstract static class PipelineBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise system(Seq processes, @CachedContext(YattaLanguage.class) Context context) {
      if (processes.length() == 0) {
        throw new BadArgException("Process pipeline must contain at least one process", this);
      }
      Object unwrappedProcesses = processes.unwrapPromises(this);
      if (unwrappedProcesses instanceof Seq) {
        return buildProcessPipeline((Seq) unwrappedProcesses, context);
      } else { // Promise
        return ((Promise) unwrappedProcesses).map((objs) -> buildProcessPipeline(Seq.sequence((Object[]) objs), context), this);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Promise buildProcessPipeline(Seq unwrappedProcesses, Context context) {
      Object processBuildersObj = unwrappedProcesses.map((processSeq) -> buildProcess(((Seq) processSeq).unwrapPromises(this), this)).unwrapPromises(this);
      if (processBuildersObj instanceof Seq) {
        return runProcessPipeline((Seq) processBuildersObj, context);
      } else { // Promise
        return ((Promise) processBuildersObj).map((objs) -> runProcessPipeline(Seq.sequence((Object[]) objs), context), this);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Promise runProcessPipeline(Seq processBuildersObj, Context context) {
      try {
        List<Process> startedProcesses = ProcessBuilder.startPipeline(Arrays.asList(processBuildersObj.toArray(ProcessBuilder.class)));
        return runProcess(startedProcesses.get(startedProcesses.size() - 1), context, this);
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @CompilerDirectives.TruffleBoundary
  private static Object buildProcess(Object sequenceObj, Node node) {
    if (sequenceObj instanceof Seq) {
      Seq sequence = (Seq) sequenceObj;
      CompilerAsserts.compilationConstant(sequence.length());
      String[] strings = new String[(int) sequence.length()];

      for (int i = 0; i < sequence.length(); i++) {
        try {
          strings[i] = TypesGen.expectSeq(sequence.lookup(i, node)).asJavaString(node);
        } catch (UnexpectedResultException e) {
          throw YattaException.typeError(node, sequence);
        }
      }

      return new ProcessBuilder().command(strings);
    } else { // Promise
      return ((Promise) sequenceObj).map((maybeSeq) -> buildProcess(Seq.sequence((Object[]) maybeSeq), node), node);
    }
  }

  @CompilerDirectives.TruffleBoundary
  private static Promise runProcess(Process process, Context context, Node node) {
    Promise stdOutPromise = new Promise();
    Promise stdErrPromise = new Promise();
    Promise exitValuePromise = new Promise();

    context.ioExecutor.submit(() -> {
      try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
        stdOutPromise.fulfil(Seq.sequence(br.lines().map(Seq::fromCharSequence).toArray()), node);
      } catch (IOException ex) {
        stdOutPromise.fulfil(new yatta.runtime.exceptions.IOException(ex, node), node);
      }
    });

    context.ioExecutor.submit(() -> {
      try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getErrorStream()))) {
        stdErrPromise.fulfil(Seq.sequence(br.lines().map(Seq::fromCharSequence).toArray()), node);
      } catch (IOException ex) {
        stdErrPromise.fulfil(new yatta.runtime.exceptions.IOException(ex, node), node);
      }
    });

    context.ioExecutor.submit(() -> {
      try {
        process.waitFor();
        exitValuePromise.fulfil(process.exitValue(), node);
      } catch (InterruptedException ex) {
        exitValuePromise.fulfil(new yatta.runtime.exceptions.IOException(ex, node), node);
      }
    });

    return Promise.all(new Object[]{stdOutPromise, stdErrPromise, exitValuePromise}, node).map(results -> {
      Object[] resultsArray = (Object[]) results;
      Seq stdOut = (Seq) resultsArray[0];
      Seq stdErr = (Seq) resultsArray[1];
      long exitValue = (int) resultsArray[2];

      return new Tuple(exitValue, stdOut, stdErr);
    }, node);
  }

  @NodeInfo(shortName = "get_env")
  abstract static class GetEnvBuiltin extends BuiltinNode {
    @Specialization
    public Object getEnv(Seq key, @CachedContext(YattaLanguage.class) Context context) {
      Map<String, String> env = context.getEnv().getEnvironment();
      String keyString = key.asJavaString(this);
      if (env.containsKey(keyString)) {
        return Seq.fromCharSequence(env.get(keyString));
      } else {
        return Unit.INSTANCE;
      }
    }
  }

  @NodeInfo(shortName = "pid")
  abstract static class PidBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public long pid() {
      return ProcessHandle.current().pid();
    }
  }

  @NodeInfo(shortName = "args")
  abstract static class GetArgsBuiltin extends BuiltinNode {
    @Specialization
    public Seq args(@CachedContext(YattaLanguage.class) Context context) {
      Seq ret = Seq.EMPTY;
      for (String arg : context.getEnv().getApplicationArguments()) {
        ret = ret.insertLast(Seq.fromCharSequence(arg));
      }
      return ret;
    }
  }

  @NodeInfo(shortName = "language_home")
  abstract static class LanguageHomeBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Seq languageHome(@CachedContext(YattaLanguage.class) Context context) {
      return context.languageHome();
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(SystemBuiltinModuleFactory.RunBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SystemBuiltinModuleFactory.PipelineBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SystemBuiltinModuleFactory.GetEnvBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SystemBuiltinModuleFactory.PidBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SystemBuiltinModuleFactory.GetArgsBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SystemBuiltinModuleFactory.LanguageHomeBuiltinFactory.getInstance()));
    return builtins;
  }
}
