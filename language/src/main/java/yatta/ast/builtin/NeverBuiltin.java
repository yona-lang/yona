package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "never")
public abstract class NeverBuiltin extends BuiltinNode {
  @Specialization
  public Promise never() {
    return Promise.NEVER;
  }
}
