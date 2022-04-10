package yona;

import com.oracle.truffle.api.*;
import com.oracle.truffle.api.debug.DebuggerTags;
import com.oracle.truffle.api.instrumentation.AllocationReporter;
import com.oracle.truffle.api.instrumentation.ProvidedTags;
import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.regex.RegexLanguage;
import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.CommonTokenStream;
import yona.ast.ExpressionNode;
import yona.ast.FunctionRootNode;
import yona.parser.*;
import yona.runtime.Context;

import java.nio.file.Path;
import java.nio.file.Paths;

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
      if (env.getEnvironment().containsKey("JAVA_HOME")) {
        languageHomePath = Paths.get(env.getEnvironment().get("JAVA_HOME"), "languages", ID);
      } else {
        languageHomePath = Paths.get(".");
        env.getLogger(getClass()).severe("JAVA_HOME environment variable must be set, otherwise stdlib from current directory is loaded. This is a potential security risk.");
      }

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
  protected void exitContext(Context context, ExitMode exitMode, int exitCode) {
    context.dispose();
  }

  @Override
  public CallTarget parse(ParsingRequest request) {
    Source source = request.getSource();
    RootCallTarget rootCallTarget = parseYona(this, Context.get(null), source);
    return rootCallTarget.getRootNode().getCallTarget();
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
    return rootNode.getRootNode().getCallTarget();
  }

  @Override
  protected boolean isVisible(Context context, Object value) {
    return context.isPrintAllResults();
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
