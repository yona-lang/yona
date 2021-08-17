package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

import java.time.Instant;

@BuiltinModuleInfo(moduleName = "Time")
public class TimeBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "unix")
  abstract static class UnixBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public long newline() {
      return Instant.now().getEpochSecond();
    }
  }

  @Override
  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(TimeBuiltinModuleFactory.UnixBuiltinFactory.getInstance())
    );
  }
}
