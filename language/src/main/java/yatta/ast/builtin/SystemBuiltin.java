package yatta.ast.builtin;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.TypesGen;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Sequence;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

@NodeInfo(shortName = "system")
public abstract class SystemBuiltin extends BuiltinNode {
  @Specialization
  @CompilerDirectives.TruffleBoundary
  public Promise system(Sequence sequence, @CachedContext(YattaLanguage.class) Context context) {
    CompilerAsserts.compilationConstant(sequence.length());
    String[] strings = new String[sequence.length()];

    sequence.foldLeft((i, item) -> {
      try {
        strings[i] = TypesGen.expectString(sequence.lookup(i));
        return i + 1;
      } catch (UnexpectedResultException e) {
        throw YattaException.typeError(this, sequence);
      }
    }, 0);

    ProcessBuilder processBuilder = new ProcessBuilder().command(strings);

    try {
      Process process = processBuilder.start();
      Promise stdOutPromise = new Promise();
      Promise stdErrPromise = new Promise();
      Promise exitValuePromise = new Promise();

      context.ioExecutor.submit(() -> {
        try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
          stdOutPromise.fulfil(Sequence.sequence(br.lines().toArray()), this);
        } catch (IOException ex) {
          stdOutPromise.fulfil(new yatta.runtime.exceptions.IOException(ex, this), this);
        }
      });

      context.ioExecutor.submit(() -> {
        try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getErrorStream()))) {
          stdErrPromise.fulfil(Sequence.sequence(br.lines().toArray()), this);
        } catch (IOException ex) {
          stdErrPromise.fulfil(new yatta.runtime.exceptions.IOException(ex, this), this);
        }
      });

      context.ioExecutor.submit(() -> {
        try {
          process.waitFor();
          exitValuePromise.fulfil(process.exitValue(), this);
        } catch (InterruptedException ex) {
          exitValuePromise.fulfil(new yatta.runtime.exceptions.IOException(ex, this), this);
        }
      });

      return Promise.all(new Object[]{stdOutPromise, stdErrPromise, exitValuePromise}, this).map(results -> {
        Object[] resultsArray = (Object[]) results;
        Sequence stdOut = (Sequence) resultsArray[0];
        Sequence stdErr = (Sequence) resultsArray[1];
        long exitValue = (int) resultsArray[2];

        return new Tuple(exitValue, stdOut, stdErr);
      }, this);
    } catch (IOException e) {
      throw new yatta.runtime.exceptions.IOException(e, this);
    }
  }
}
