package abzu;

import abzu.ast.builtin.BuiltinNode;
import abzu.ast.local.LexicalScope;
import abzu.runtime.Context;
import abzu.runtime.Function;
import abzu.runtime.Module;
import abzu.runtime.Unit;
import com.oracle.truffle.api.*;
import com.oracle.truffle.api.debug.DebuggerTags;
import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.frame.Frame;
import com.oracle.truffle.api.instrumentation.ProvidedTags;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;

import java.util.*;

@TruffleLanguage.Registration(id = AbzuLanguage.ID, name = "abzu", defaultMimeType = AbzuLanguage.MIME_TYPE, characterMimeTypes = AbzuLanguage.MIME_TYPE, contextPolicy = TruffleLanguage.ContextPolicy.SHARED, fileTypeDetectors = FiletypeDetector.class)
@ProvidedTags({StandardTags.CallTag.class, StandardTags.StatementTag.class, StandardTags.RootTag.class, StandardTags.ExpressionTag.class, DebuggerTags.AlwaysHalt.class})
public class AbzuLanguage extends TruffleLanguage<Context> {

  public static final String ID = "abzu";
  public static final String MIME_TYPE = "application/x-abzu";

  public AbzuLanguage() {
    super();
  }

  @Override
  protected Context createContext(Env env) {
    return new Context(this, env, new ArrayList<>(EXTERNAL_BUILTINS));
  }

  @Override
  public CallTarget parse(ParsingRequest request) throws Exception {
    Source source = request.getSource();
    RootCallTarget rootCallTarget;
    /*
     * Parse the provided source. At this point, we do not have a Context yet. Registration of
     * the functions with the Context happens lazily in AbzuEvalRootNode.
     */
    rootCallTarget = AbzuParser.parseAbzu(this, source);

    return Truffle.getRuntime().createCallTarget(rootCallTarget.getRootNode());
  }

  @Override
  protected boolean isVisible(Context context, Object value) {
    return !InteropLibrary.getFactory().getUncached(value).isNull(value);

  }

  @Override
  protected boolean isObjectOfLanguage(Object object) {
    if (!(object instanceof TruffleObject)) {
      return false;
    }
    TruffleObject truffleObject = (TruffleObject) object;
    return truffleObject instanceof Function;
  }

  @Override
  protected String toString(Context context, Object value) {
    return toString(value);
  }

  public static String toString(Object value) {
    try {
      if (value == null) {
        return "ANY";
      }
      InteropLibrary interop = InteropLibrary.getFactory().getUncached(value);
      if (interop.fitsInLong(value)) {
        return Long.toString(interop.asLong(value));
      } else if (interop.fitsInDouble(value)) {
        return Double.toString(interop.asDouble(value));
      } else if (interop.isBoolean(value)) {
        return Boolean.toString(interop.asBoolean(value));
      } else if (interop.isString(value)) {
        return interop.asString(value);
      } else if (interop.isNull(value)) {
        return "NONE";
      } else if (interop.isExecutable(value)) {
        if (value instanceof Function) {
          return ((Function) value).getName();
        } else {
          return "Function";
        }
      } else {
        if(value == Unit.INSTANCE) {
          return "()";
        } else if (value instanceof Module) {
          return ((Module) value).toString();
        } else {
          return "Unsupported";
        }
      }
    } catch (UnsupportedMessageException e) {
      CompilerDirectives.transferToInterpreter();
      throw new AssertionError();
    }
  }

  @Override
  protected Object findMetaObject(Context context, Object value) {
    return getMetaObject(value);
  }

  public static String getMetaObject(Object value) {
    if (value == null) {
      return "ANY";
    }
    InteropLibrary interop = InteropLibrary.getFactory().getUncached(value);
    if (interop.isNumber(value)) {
      return "Number";
    } else if (interop.isBoolean(value)) {
      return "Boolean";
    } else if (interop.isString(value)) {
      return "String";
    } else if (interop.isExecutable(value)) {
      return "Function";
    } else {
      if (value == Unit.INSTANCE) {
        return "Unit";
      } else if (value instanceof Module) {
        return "Module";
      } else {
        return "Unsupported";
      }
    }
  }


  @Override
  protected SourceSection findSourceLocation(Context context, Object value) {
    if (value instanceof Function) {
      Function f = (Function) value;
      return f.getDeclaredLocation();
    }
    return null;
  }

  @Override
  public Iterable<Scope> findLocalScopes(Context context, Node node, Frame frame) {
    final LexicalScope scope = LexicalScope.createScope(node);
    return () -> new Iterator<Scope>() {
      private LexicalScope previousScope;
      private LexicalScope nextScope = scope;

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

  public static Context getCurrentContext() {
    return getCurrentContext(AbzuLanguage.class);
  }

  private static final List<NodeFactory<? extends BuiltinNode>> EXTERNAL_BUILTINS = Collections.synchronizedList(new ArrayList<>());

  public static void installBuiltin(NodeFactory<? extends BuiltinNode> builtin) {
    EXTERNAL_BUILTINS.add(builtin);
  }
}
