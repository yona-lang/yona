package yona;

import com.oracle.truffle.api.*;
import com.oracle.truffle.api.debug.DebuggerTags;
import com.oracle.truffle.api.instrumentation.ProvidedTags;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import com.oracle.truffle.regex.RegexLanguage;
import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.CommonTokenStream;
import org.antlr.v4.runtime.TokenStreamRewriter;
import yona.ast.ExpressionNode;
import yona.ast.FunctionRootNode;
import yona.parser.*;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.Unit;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;

@TruffleLanguage.Registration(id = YonaLanguage.ID, name = YonaLanguage.NAME, defaultMimeType = YonaLanguage.MIME_TYPE, characterMimeTypes = YonaLanguage.MIME_TYPE, contextPolicy = TruffleLanguage.ContextPolicy.SHARED, fileTypeDetectors = FiletypeDetector.class, dependentLanguages = {RegexLanguage.ID})
@ProvidedTags({StandardTags.CallTag.class, StandardTags.StatementTag.class, StandardTags.RootTag.class, StandardTags.ExpressionTag.class, StandardTags.ReadVariableTag.class, StandardTags.WriteVariableTag.class, DebuggerTags.AlwaysHalt.class})
public class YonaLanguage extends TruffleLanguage<Context> {
  public static final String ID = "yona";
  public static final String NAME = "Yona";
  public static final String MIME_TYPE = "application/x-yona";

  public YonaLanguage() {
    super();
  }

  @Override
  protected Context createContext(Env env) {
    String languageHome = getLanguageHome();
    Path languageHomePath, stdlibHomePath;

    if (languageHome == null) {
      languageHomePath = Paths.get(env.getEnvironment().get("JAVA_HOME"), "languages", ID);

      if (env.getEnvironment().containsKey("YONA_STDLIB_HOME")) {
        stdlibHomePath = Paths.get(env.getEnvironment().get("YONA_STDLIB_HOME"));
      } else {
        stdlibHomePath = Paths.get(languageHomePath.toFile().getAbsolutePath(), "lib-yona");
      }
    } else {
      languageHomePath = Path.of(languageHome);
      stdlibHomePath = Paths.get(languageHome, "lib-yona");
    }

    return new Context(this, env, languageHomePath, stdlibHomePath);
  }

  @Override
  protected void initializeContext(Context context) throws Exception {
    context.initialize();
  }

  @Override
  protected void finalizeContext(Context context) {
    context.dispose();
  }

  @Override
  public CallTarget parse(ParsingRequest request) {
    Source source = request.getSource();
    RootCallTarget rootCallTarget = parseYona(this, getCurrentContext(), source);
    return Truffle.getRuntime().createCallTarget(rootCallTarget.getRootNode());
  }

  private static ExpressionNode parseYonaExpression(YonaLanguage language, Context context, Source source) {
    YonaLexer lexer = new YonaLexer(CharStreams.fromString(source.getCharacters().toString()));
    YonaParser parser = new YonaParser(new CommonTokenStream(lexer));
    lexer.removeErrorListeners();
    parser.removeErrorListeners();
    YonaErrorListener listener = new YonaErrorListener(source);
    lexer.addErrorListener(listener);
    parser.addErrorListener(listener);
    parser.setErrorHandler(new YonaErrorStrategy(source));
    return new ParserVisitor(language, context, source).visit(parser.input());
  }

  private static RootCallTarget parseYona(YonaLanguage language, Context context, Source source) {
    ExpressionNode rootExpression = parseYonaExpression(language, context, source);
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
    return getCurrentContext(YonaLanguage.class);
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
