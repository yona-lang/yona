package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Seq;

import java.io.IOException;

@NodeInfo(shortName = "readln")
public abstract class ReadlnBuiltin extends BuiltinNode {
  @Specialization
  public Seq readln(@CachedContext(YattaLanguage.class) Context context) {
    try {
      return Seq.fromCharSequence(context.getInput().readLine());
    } catch (IOException e) {
      throw new yatta.runtime.exceptions.IOException(e, this);
    }
  }
}
