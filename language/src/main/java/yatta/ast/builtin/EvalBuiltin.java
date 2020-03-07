package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Seq;

@NodeInfo(shortName = "eval")
public abstract class EvalBuiltin extends BuiltinNode {
  @Specialization
  public Object eval(Seq source, @CachedContext(YattaLanguage.class) Context context) {
    return context.getEnv().parsePublic(Source.newBuilder(YattaLanguage.ID, source.asJavaString(this), "eval").build()).call();
  }
}
