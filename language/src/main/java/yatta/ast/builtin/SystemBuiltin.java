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
import yatta.runtime.Seq;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

@NodeInfo(shortName = "system")
public abstract class SystemBuiltin extends BuiltinNode {
  @Specialization
  @CompilerDirectives.TruffleBoundary
  public Promise system(Seq sequence, @CachedContext(YattaLanguage.class) Context context) {
    CompilerAsserts.compilationConstant(sequence.length());
    String[] strings = new String[(int) sequence.length()];

    for(int i = 0; i < sequence.length(); i++) {
      try {
        strings[i] = TypesGen.expectString(sequence.lookup(i, this));
      } catch (UnexpectedResultException e) {
        throw YattaException.typeError(this, sequence);
      }
    }

    ProcessBuilder processBuilder = new ProcessBuilder().command(strings);

    try {
      Process process = processBuilder.start();
      Promise stdOutPromise = new Promise();
      Promise stdErrPromise = new Promise();
      Promise exitValuePromise = new Promise();

      context.ioExecutor.submit(() -> {
        try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
          stdOutPromise.fulfil(Seq.sequence(br.lines().toArray()), this);
        } catch (IOException ex) {
          stdOutPromise.fulfil(new yatta.runtime.exceptions.IOException(ex, this), this);
        }
      });

      context.ioExecutor.submit(() -> {
        try (BufferedReader br = new BufferedReader(new InputStreamReader(process.getErrorStream()))) {
          stdErrPromise.fulfil(Seq.sequence(br.lines().toArray()), this);
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
        Seq stdOut = (Seq) resultsArray[0];
        Seq stdErr = (Seq) resultsArray[1];
        long exitValue = (int) resultsArray[2];

        return new Tuple(exitValue, stdOut, stdErr);
      }, this);
    } catch (IOException e) {
      throw new yatta.runtime.exceptions.IOException(e, this);
    }
  }
}
