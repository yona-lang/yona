package abzu;

import abzu.ast.builtin.AbzuBuiltinNode;
import abzu.ast.local.AbzuLexicalScope;
import abzu.runtime.AbzuContext;
import abzu.runtime.AbzuFunction;
import abzu.runtime.AbzuUnit;
import com.oracle.truffle.api.*;
import com.oracle.truffle.api.debug.DebuggerTags;
import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.frame.Frame;
import com.oracle.truffle.api.instrumentation.ProvidedTags;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;

import java.util.*;

@TruffleLanguage.Registration(id = AbzuLanguage.ID, name = "smol", defaultMimeType = AbzuLanguage.MIME_TYPE, characterMimeTypes = AbzuLanguage.MIME_TYPE, contextPolicy = TruffleLanguage.ContextPolicy.SHARED)
@ProvidedTags({StandardTags.CallTag.class, StandardTags.StatementTag.class, StandardTags.RootTag.class, StandardTags.ExpressionTag.class, DebuggerTags.AlwaysHalt.class})
public class AbzuLanguage extends TruffleLanguage<AbzuContext> {
  public static volatile int counter;

  public static final String ID = "abzu";
  public static final String MIME_TYPE = "application/x-abzu";

  public AbzuLanguage() {
    counter++;
  }

  @Override
  protected AbzuContext createContext(Env env) {
    return new AbzuContext(this, env, new ArrayList<>(EXTERNAL_BUILTINS));
  }

  @Override
  protected CallTarget parse(ParsingRequest request) throws Exception {
    Source source = request.getSource();
    RootCallTarget rootCallTarget;
    /*
     * Parse the provided source. At this point, we do not have a AbzuContext yet. Registration of
     * the functions with the AbzuContext happens lazily in AbzuEvalRootNode.
     */
    rootCallTarget = AbzuParser.parseAbzu(this, source);

    return Truffle.getRuntime().createCallTarget(rootCallTarget.getRootNode());
  }

  @Override
  protected boolean isVisible(AbzuContext context, Object value) {
    return value != AbzuUnit.INSTANCE;
  }

  @Override
  protected boolean isObjectOfLanguage(Object object) {
    if (!(object instanceof TruffleObject)) {
      return false;
    }
    TruffleObject truffleObject = (TruffleObject) object;
    return truffleObject instanceof AbzuFunction || AbzuContext.isAbzuObject(truffleObject);
  }

  @Override
  protected String toString(AbzuContext context, Object value) {
    if (value == AbzuUnit.INSTANCE) {
      return "NONE";
    }
    if (value instanceof Long) {
      return Long.toString((Long) value);
    }
    return super.toString(context, value);
  }

  @Override
  protected Object findMetaObject(AbzuContext context, Object value) {
    if (value instanceof Number) {
      return "Number";
    }
    if (value instanceof Boolean) {
      return "Boolean";
    }
    if (value instanceof String) {
      return "String";
    }
    if (value == AbzuUnit.INSTANCE) {
      return "None";
    }
    if (value instanceof AbzuFunction) {
      return "Function";
    }
    return "Object";
  }

  @Override
  protected SourceSection findSourceLocation(AbzuContext context, Object value) {
    if (value instanceof AbzuFunction) {
      AbzuFunction f = (AbzuFunction) value;
      return f.getCallTarget().getRootNode().getSourceSection();
    }
    return null;
  }

  @Override
  public Iterable<Scope> findLocalScopes(AbzuContext context, Node node, Frame frame) {
    final AbzuLexicalScope scope = AbzuLexicalScope.createScope(node);
    return new Iterable<Scope>() {
      @Override
      public Iterator<Scope> iterator() {
        return new Iterator<Scope>() {
          private AbzuLexicalScope previousScope;
          private AbzuLexicalScope nextScope = scope;

          @Override
          public boolean hasNext() {
            if (nextScope == null) {
              nextScope = previousScope.findParent();
            }
            return nextScope != null;
          }

          @Override
          public Scope next() {
            if (!hasNext()) {
              throw new NoSuchElementException();
            }
            Scope vscope = Scope.newBuilder(nextScope.getName(), nextScope.getVariables(frame)).node(nextScope.getNode()).arguments(nextScope.getArguments(frame)).build();
            previousScope = nextScope;
            nextScope = null;
            return vscope;
          }
        };
      }
    };
  }

  @Override
  protected Iterable<Scope> findTopScopes(AbzuContext context) {
    return context.getTopScopes();
  }

  public static AbzuContext getCurrentContext() {
    return getCurrentContext(AbzuLanguage.class);
  }

  private static final List<NodeFactory<? extends AbzuBuiltinNode>> EXTERNAL_BUILTINS = Collections.synchronizedList(new ArrayList<>());

  public static void installBuiltin(NodeFactory<? extends AbzuBuiltinNode> builtin) {
    EXTERNAL_BUILTINS.add(builtin);
  }
}
