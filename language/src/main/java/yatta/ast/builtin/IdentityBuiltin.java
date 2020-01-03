package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "identity")
public abstract class IdentityBuiltin extends BuiltinNode {
  @Specialization
  public <T> T async(T value) {
    return value;
  }
}

