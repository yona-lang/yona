package yona.ast.builtin;

import com.oracle.truffle.api.CallTarget;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.dsl.Cached;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.DirectCallNode;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;
import yona.YonaLanguage;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Symbol;
import yona.runtime.exceptions.PolyglotException;

@NodeInfo(shortName = "eval")
public abstract class EvalBuiltin extends BuiltinNode {
  static final int LIMIT = 2;

  @Specialization(guards = {"cachedId.equals(id)", "cachedCode.equals(code)"}, limit = "LIMIT")
  public Object evalCached(Symbol id, Seq code,
                           @Cached("id") Symbol cachedId,
                           @Cached("code") Seq cachedCode,
                           @CachedContext(YonaLanguage.class) Context context,
                           @Cached("create(parse(id, code, context))") DirectCallNode callNode) {
    return callNode.call();
  }

  @CompilerDirectives.TruffleBoundary
  @Specialization(replaces = "evalCached")
  public Object evalUncached(Symbol id, Seq code, @CachedContext(YonaLanguage.class) Context context) {
    return parse(id, code, context).call();
  }

  protected CallTarget parse(Symbol id, Seq code, Context context) {
    TruffleLanguage.Env env = context.getEnv();
    if (env.isPolyglotEvalAllowed()) {
      final Source source = Source.newBuilder(id.asString(), code.asJavaString(this), "eval").build();
      return env.parsePublic(source);
    } else {
      throw new PolyglotException("Polyglot eval is not allowed", this);
    }
  }
}
