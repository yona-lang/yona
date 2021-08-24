package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

import java.time.Instant;
import java.util.concurrent.ThreadLocalRandom;

@BuiltinModuleInfo(moduleName = "Random")
public final class RandomBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "integer_lt")
  abstract static class IntegerLtBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public long integerLt(long bound) {
      return ThreadLocalRandom.current().nextLong(bound);
    }
  }

  @Override
  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(RandomBuiltinModuleFactory.IntegerLtBuiltinFactory.getInstance())
    );
  }
}
