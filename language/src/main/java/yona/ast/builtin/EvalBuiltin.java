package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;
import yona.YonaLanguage;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Symbol;
import yona.runtime.exceptions.PolyglotException;

@NodeInfo(shortName = "eval")
public abstract class EvalBuiltin extends BuiltinNode {
  @Specialization
  @CompilerDirectives.TruffleBoundary
  public Object eval(Symbol language, Seq source, @CachedContext(YonaLanguage.class) Context context) {
    TruffleLanguage.Env env = context.getEnv();
    if (env.isPolyglotEvalAllowed()) {
      return env.parsePublic(Source.newBuilder(language.asString(), source.asJavaString(this), "eval").build()).call();
    } else {
      throw new PolyglotException("Polyglot eval is not allowed", this);
    }
  }
}
