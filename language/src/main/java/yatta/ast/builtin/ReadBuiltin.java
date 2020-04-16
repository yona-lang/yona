package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Seq;

import java.io.IOException;

@NodeInfo(shortName = "read")
public abstract class ReadBuiltin extends BuiltinNode {
  @Specialization
  public int read(@CachedContext(YattaLanguage.class) Context context) {
    try {
      return context.getInput().read();
    } catch (IOException e) {
      throw new yatta.runtime.exceptions.IOException(e, this);
    }
  }
}
