package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Seq;
import yona.runtime.Tuple;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;
import yona.runtime.stdlib.PrivateFunction;

import java.time.Instant;

@BuiltinModuleInfo(moduleName = "Exception")
public final class ExceptionBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "pretty_print_frame")
  abstract static class PrettyPrintFrameBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Seq prettyPrintFrame(Tuple tuple) {
      return Seq.fromCharSequence(YonaException.stackFrameTupleToString(tuple));
    }
  }

  @Override
  public Builtins builtins() {
    return new Builtins(
        new PrivateFunction(ExceptionBuiltinModuleFactory.PrettyPrintFrameBuiltinFactory.getInstance())
    );
  }
}
