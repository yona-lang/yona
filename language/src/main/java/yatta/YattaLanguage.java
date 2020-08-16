package yatta;

import com.oracle.truffle.api.*;
import com.oracle.truffle.api.debug.DebuggerTags;
import com.oracle.truffle.api.instrumentation.ProvidedTags;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.CommonTokenStream;
import yatta.ast.ExpressionNode;
import yatta.ast.FunctionRootNode;
import yatta.ast.ShutdownNode;
import yatta.parser.*;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.Unit;

import java.nio.file.Path;
import java.nio.file.Paths;

@TruffleLanguage.Registration(id = YattaLanguage.ID, name = YattaLanguage.NAME, defaultMimeType = YattaLanguage.MIME_TYPE, characterMimeTypes = YattaLanguage.MIME_TYPE, contextPolicy = TruffleLanguage.ContextPolicy.SHARED, fileTypeDetectors = FiletypeDetector.class, dependentLanguages = {"regex"})
@ProvidedTags({StandardTags.CallTag.class, StandardTags.StatementTag.class, StandardTags.RootTag.class, StandardTags.ExpressionTag.class, StandardTags.ReadVariableTag.class, StandardTags.WriteVariableTag.class, DebuggerTags.AlwaysHalt.class})
public class YattaLanguage extends TruffleLanguage<Context> {
  public static final String ID = "yatta";
  public static final String NAME = "Yatta";
  public static final String MIME_TYPE = "application/x-yatta";

  public YattaLanguage() {
    super();
  }

  @Override
  protected Context createContext(Env env) {
    String languageHome = getLanguageHome();
    Path languageHomePath, stdlibHomePath;

    if (languageHome == null) {
      languageHomePath = Paths.get(env.getEnvironment().get("JAVA_HOME"), "languages", ID);

      if (env.getEnvironment().containsKey("YATTA_STDLIB_HOME")) {
        stdlibHomePath = Paths.get(env.getEnvironment().get("YATTA_STDLIB_HOME"));
      } else {
        stdlibHomePath = Paths.get(languageHomePath.toFile().getAbsolutePath(), "lib-yatta");
      }
    } else {
      languageHomePath = Path.of(languageHome);
      stdlibHomePath = Paths.get(languageHome, "lib-yatta");
    }

    return new Context(this, env, languageHomePath, stdlibHomePath);
  }

  @Override
  protected void initializeContext(Context context) throws Exception {
    context.initialize();
  }

  @Override
  public CallTarget parse(ParsingRequest request) {
    Source source = request.getSource();
    if (source.equals(Context.SHUTDOWN_SOURCE)) {
      return Truffle.getRuntime().createCallTarget(new ShutdownNode(this));
    } else {
      RootCallTarget rootCallTarget = parseYatta(this, getCurrentContext(), source);
      return Truffle.getRuntime().createCallTarget(rootCallTarget.getRootNode());
    }
  }

  private static ExpressionNode parseYattaExpression(YattaLanguage language, Context context, Source source) {
    YattaLexer lexer = new YattaLexer(CharStreams.fromString(source.getCharacters().toString()));
    YattaParser parser = new YattaParser(new CommonTokenStream(lexer));
    lexer.removeErrorListeners();
    parser.removeErrorListeners();
    YattaErrorListener listener = new YattaErrorListener(source);
    lexer.addErrorListener(listener);
    parser.addErrorListener(listener);
    parser.setErrorHandler(new YattaErrorStrategy(source));
    return new ParserVisitor(language, context, source).visit(parser.input());
  }

  private static RootCallTarget parseYatta(YattaLanguage language, Context context, Source source) {
    ExpressionNode rootExpression = parseYattaExpression(language, context, source);
    FunctionRootNode rootNode = new FunctionRootNode(language, context.globalFrameDescriptor, rootExpression, source.createSection(1), null, "root");
    return Truffle.getRuntime().createCallTarget(rootNode);
  }

  @Override
  protected boolean isVisible(Context context, Object value) {
    return context.isPrintAllResults();
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
        return "()";
      } else if (interop.isExecutable(value)) {
        if (value instanceof Function) {
          return ((Function) value).getName();
        } else {
          return "Function";
        }
      } else {
        if (value == Unit.INSTANCE) {
          return "()";
        } else if (value instanceof Module) {
          return ((Module) value).toString();
        } else {
          return "Unsupported";
        }
      }
    } catch (UnsupportedMessageException e) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
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

  public static Context getCurrentContext() {
    return getCurrentContext(YattaLanguage.class);
  }

  @Override
  protected boolean isThreadAccessAllowed(Thread thread, boolean singleThreaded) {
    return true;
  }

  @CompilerDirectives.TruffleBoundary
  public static TruffleLogger getLogger(Class<?> clazz) {
    return TruffleLogger.getLogger(ID, clazz);
  }
}
