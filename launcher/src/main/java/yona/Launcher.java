package yona;

import akovari.antlr4.autocomplete.Antlr4Completer;
import akovari.antlr4.autocomplete.CompletionResult;
import akovari.antlr4.autocomplete.DefaultLexerAndParserFactory;
import jline.console.UserInterruptException;
import net.sourceforge.argparse4j.ArgumentParsers;
import net.sourceforge.argparse4j.annotation.Arg;
import net.sourceforge.argparse4j.inf.ArgumentParser;
import net.sourceforge.argparse4j.inf.ArgumentParserException;
import org.graalvm.launcher.AbstractLanguageLauncher;
import org.graalvm.nativeimage.ImageInfo;
import org.graalvm.nativeimage.ProcessProperties;
import org.graalvm.options.OptionCategory;
import org.graalvm.polyglot.*;
import org.jline.reader.Candidate;
import yona.parser.YonaLexer;
import yona.parser.YonaParser;

import java.io.*;
import java.lang.management.ManagementFactory;
import java.nio.file.NoSuchFileException;
import java.nio.file.Paths;
import java.util.*;

import static net.sourceforge.argparse4j.impl.Arguments.fileType;
import static net.sourceforge.argparse4j.impl.Arguments.storeTrue;

public final class Launcher extends AbstractLanguageLauncher {
  public static void main(String[] args) {
    new Launcher().launch(args);
  }

  // provided by GraalVM bash launchers, ignored in native image mode
  private static final String BASH_LAUNCHER_EXEC_NAME = System.getProperty("org.graalvm.launcher.executablename");

  private final static String LANGUAGE_ID = "yona";
  private static final String MIME_TYPE = "application/x-yona";
  private static final String PROGRAM_NAME = BASH_LAUNCHER_EXEC_NAME != null ? BASH_LAUNCHER_EXEC_NAME : LANGUAGE_ID;

  private final List<String> programArgs = new ArrayList<>();
  private String commandString = null;
  private String inputFile = null;
  private VersionAction versionAction = VersionAction.None;
  private List<String> relaunchArgs;
  private boolean verboseFlag = false;
  private List<String> givenArguments;

  private final ArgumentParser argumentParser;

  static final Set<String> DISALLOWED_COMPLETION_STATES = Set.of(
      "CHARACTER_LITERAL", "REGULAR_CHAR_INSIDE", "REGULAR_STRING_INSIDE", "SYMBOL",

      "BYTE", "FLOAT_INTEGER", "INTEGER", "FLOAT",

      "OP_EQ", "OP_NEQ",
      "OP_LT", "OP_LTE", "OP_GT", "OP_GTE", "OP_RIGHT_ARROW", "OP_LEFT_ARROW",
      "OP_POWER", "OP_MULTIPLY", "OP_DIVIDE", "OP_MODULO", "OP_PLUS", "OP_MINUS",
      "OP_LEFTSHIFT", "OP_RIGHTSHIFT", "OP_ZEROFILL_RIGHTSHIFT", "OP_BIN_AND",
      "OP_BIN_XOR", "OP_BIN_NOT", "OP_LOGIC_AND", "OP_LOGIC_OR", "OP_LOGIC_NOT",
      "OP_CONS_L", "OP_CONS_R", "OP_JOIN", "OP_PIPE_L", "OP_PIPE_R",

      "InputCharacter", "NewLinePart", "WHITESPACE", "UnicodeClassZS",
      "CommonCharacter", "SimpleEscapeSequence", "HexEscapeSequence", "UnicodeEscapeSequence",
      "HexDigit", "UnicodeClassLU", "UnicodeClassLL", "UnicodeClassLT", "UnicodeClassLM",
      "UnicodeClassLO", "UnicodeClassNL", "UnicodeClassMN", "UnicodeClassMC",
      "UnicodeClassCF", "UnicodeClassPC", "UnicodeClassND"
  );

  private final DefaultLexerAndParserFactory<YonaLexer, YonaParser> lexerAndParserFactory = new DefaultLexerAndParserFactory<>(YonaLexer::new, YonaParser::new, (state) -> !Launcher.DISALLOWED_COMPLETION_STATES.contains(state));

  public Launcher() {
    this.argumentParser = ArgumentParsers.newFor(PROGRAM_NAME).build().defaultHelp(true).description("Yona Language Interpreter");
    argumentParser.addArgument("-c", "--command").help("Command to execute");
    argumentParser.addArgument("-f", "--file").type(fileType()).help("File to execute");
    argumentParser.addArgument("-v", "--verbose").help("Set fine logging on").action(storeTrue());
    argumentParser.addArgument("-V", "--version").help("Print Yona version and exit").action(storeTrue());
    argumentParser.addArgument("--show-version").help("Print Yona version and start REPL").action(storeTrue()).dest("showVersion");
    argumentParser.addArgument("-debug-java").help("Enable Java debugger on port 8000").action(storeTrue()).dest("debugJava");
    argumentParser.addArgument("-debug-perf").help("Enable performance logging").action(storeTrue()).dest("debugPerf");
    argumentParser.addArgument("-compile-truffle-immediately").help("Enable performance logging").action(storeTrue()).dest("compileTruffleImmediately");
    argumentParser.epilog("""
        Other environment variables:
          YONA_STDLIB_HOME                     location of the standard library (provided by Yona distribution by default)
          YONA_PATH                            ':'-separated list of directories prefixed to the default module search path""");
  }

  @Override
  protected void printHelp(OptionCategory maxCategory) {
    argumentParser.printHelp();
  }

  private static final class LauncherArgs {
    @Arg
    public String command;
    @Arg
    public File file;
    @Arg
    public boolean verbose;
    @Arg
    public boolean version;
    @Arg
    boolean showVersion;
    @Arg
    boolean debugJava;
    @Arg
    boolean debugPerf;
    @Arg
    boolean compileTruffleImmediately;
  }

  @Override
  protected List<String> preprocessArguments(List<String> givenArgs, Map<String, String> polyglotOptions) {
    List<String> unrecognized = new ArrayList<>();
    List<String> defaultEnvironmentArgs = getDefaultEnvironmentArgs();
    List<String> inputArgs = new ArrayList<>(defaultEnvironmentArgs);
    inputArgs.addAll(givenArgs);
    givenArguments = new ArrayList<>(inputArgs);
    List<String> subprocessArgs = new ArrayList<>();

    try {
      LauncherArgs parsedArgs = new LauncherArgs();
      argumentParser.parseKnownArgs(givenArgs.toArray(new String[]{}), unrecognized, parsedArgs);

      if (parsedArgs.command != null) {
        programArgs.add(parsedArgs.command);
        commandString = parsedArgs.command;
      }

      if (parsedArgs.verbose) {
        verboseFlag = true;
      }

      if (parsedArgs.version) {
        versionAction = VersionAction.PrintAndExit;
      }

      if (parsedArgs.showVersion) {
        versionAction = VersionAction.PrintAndContinue;
      }

      if (parsedArgs.debugJava) {
        if (!isAOT()) {
          subprocessArgs.add("Xrunjdwp:transport=dt_socket,server=y,address=8000,suspend=y");
          inputArgs.remove("-debug-java");
        }
      }

      if (parsedArgs.debugPerf) {
        subprocessArgs.add("Dgraal.TraceTruffleCompilation=true");
        subprocessArgs.add("Dgraal.TraceTrufflePerformanceWarnings=true");
        subprocessArgs.add("Dgraal.TruffleCompilationExceptionsArePrinted=true");
        subprocessArgs.add("Dgraal.TraceTruffleInlining=true");
        subprocessArgs.add("Dgraal.TruffleTraceSplittingSummary=true");
        subprocessArgs.add("Dgraal.TraceTruffleTransferToInterpreter=true");
        subprocessArgs.add("Dgraal.TraceTruffleAssumptions=true");
        inputArgs.remove("-debug-perf");
      }

      if (parsedArgs.compileTruffleImmediately) {
        subprocessArgs.add("Dgraal.TruffleCompileImmediately=true");
        subprocessArgs.add("Dgraal.TruffleCompilationExceptionsAreThrown=true");
        inputArgs.remove("-compile-truffle-immediately");
      }

      if (parsedArgs.file != null) {
        inputFile = parsedArgs.file.getPath();
        programArgs.add(inputFile);
      }
    } catch (ArgumentParserException e) {
      argumentParser.handleError(e);
      System.exit(1);
    }

    if (!subprocessArgs.isEmpty()) {
      subExec(inputArgs, subprocessArgs);
    }

    programArgs.addAll(unrecognized.stream().filter(arg -> !arg.startsWith("-")).toList());
    unrecognized = unrecognized.stream().filter(arg -> arg.startsWith("-")).toList();

    return unrecognized;
  }

  @Override
  protected void launch(Context.Builder contextBuilder) {
    if (verboseFlag) {
      contextBuilder.option("log.yona.level", "FINE");
    }

    int rc = 1;
    try (Context context = contextBuilder
        .arguments(getLanguageId(), programArgs.toArray(String[]::new))
        .in(ConsoleHandler.createInputStream())
        .build()) {
      ConsoleHandler consoleHandler = createConsoleHandler(System.in, System.out, context);
      ConsoleHandler.INSTANCE = consoleHandler;

      runVersionAction(versionAction, context.getEngine());
      consoleHandler.setContext(context);

      if (commandString != null || inputFile != null) {
        try {
          evalNonInteractive(context);
          rc = 0;
        } catch (PolyglotException e) {
          if (!e.isExit()) {
            printStackTrace(e);
          } else {
            rc = e.getExitStatus();
          }
        } catch (NoSuchFileException e) {
          printFileNotFoundException(e);
        }
      }
      if (commandString == null && inputFile == null) {
        rc = readEvalPrint(context, consoleHandler);
      }
    } catch (IOException e) {
      rc = 1;
      e.printStackTrace();
    }
    System.exit(rc);
  }

  @Override
  protected String getLanguageId() {
    return LANGUAGE_ID;
  }

  public ConsoleHandler createConsoleHandler(InputStream inStream, OutputStream outStream, Context context) {
    if (inputFile != null || commandString != null) {
      return new DefaultConsoleHandler(inStream, outStream);
    }

    String languageHome = context.eval(Source.newBuilder(getLanguageId(), "System::language_home", "<stdin>").interactive(false).buildLiteral()).asString();

    return new JLineConsoleHandler(Paths.get(languageHome, "yona.nanorc"), inStream, outStream, (buffer) -> {
      CompletionResult completionResult = new Antlr4Completer(lexerAndParserFactory, buffer).complete();
      Set<String> suggestions = completionResult.getSuggestions();
      Set<Candidate> candidates = new HashSet<>();

      for (String suggestion : suggestions) {
        candidates.add(new Candidate(suggestion, suggestion, "syntax", null, null, null, suggestion.equals("end")));
      }

      List<CompletionResult.InputToken> tokens = completionResult.getTokens();
      String autocompleteRequest = "Reflect::autocomplete \"" + tokensAsString(tokens) + completionResult.getUntokenizedText() + "\" |> Set::to_seq";
      Value autocompleteResult = context.eval(Source.newBuilder(getLanguageId(), autocompleteRequest, "<stdin>").interactive(false).buildLiteral());

      for (int i = 0; i < autocompleteResult.getArraySize(); i++) {
        String suggestion = autocompleteResult.getArrayElement(i).asString();
        candidates.add(new Candidate(suggestion, suggestion, "functions", null, null, null, Character.isLowerCase(suggestion.codePointAt(0))));
      }

      return candidates;
    });
  }

  private String tokensAsString(List<CompletionResult.InputToken> tokens) {
    StringBuilder sb = new StringBuilder();

    for (CompletionResult.InputToken token : tokens) {
      sb.append(token.getText());
    }

    return sb.toString();
  }

  private void evalNonInteractive(Context context) throws IOException {
    Source src;
    if (commandString != null) {
      src = Source.newBuilder(LANGUAGE_ID, commandString, "<string>").build();
    } else {
      assert inputFile != null;
      File f = new File(inputFile);
      src = Source.newBuilder(LANGUAGE_ID, f).mimeType(MIME_TYPE).build();
    }
    context.eval(src);
  }

  static final String NORMAL_PROMPT = ConsoleColor.Blink.colorize(">>> ");
  static final String CONTINUE_PROMPT = ConsoleColor.Normal.colorize("... ");

  /**
   * The read-eval-print loop, which can take input from a console, command line expression or a
   * file. There are two ways the repl can terminate:
   * <ol>
   * <li>A {@code quit} command is executed successfully.</li>
   * <li>EOF on the input.</li>
   * </ol>
   * In case 2, we must implicitly execute a {@code quit("default, 0L, TRUE} command before
   * exiting. So,in either case, we never return.
   */
  public int readEvalPrint(Context context, ConsoleHandler consoleHandler) {
    System.out.println(ConsoleColor.BgRed.and(ConsoleColor.HighIntensity.and(ConsoleColor.White)).colorize("Welcome to Yona REPL. Type in an expression and press enter to execute or Ctrl-D to exit."));
    System.out.println();
    int lastStatus = 0;
    try {
      while (true) {
        consoleHandler.setPrompt(NORMAL_PROMPT);

        try {
          String input = consoleHandler.readLine();
          if (input == null) {
            throw new EOFException();
          }
          if (input.isEmpty() || input.charAt(0) == '#') {
            continue;
          }

          StringBuilder sb = new StringBuilder(input).append('\n');

          while (true) { // processing subsequent lines while input is incomplete
            lastStatus = 0;
            try {
              context.eval(Source.newBuilder(getLanguageId(), sb.toString(), "<stdin>").interactive(true).buildLiteral());
            } catch (PolyglotException e) {
              if (e.isIncompleteSource()) {
                // read more input until we get an empty line
                consoleHandler.setPrompt(CONTINUE_PROMPT);
                String additionalInput = consoleHandler.readLine();
                while (additionalInput != null && !additionalInput.isBlank()) {
                  sb.append(additionalInput).append('\n');
                  additionalInput = consoleHandler.readLine();
                }

                if (additionalInput == null) {
                  throw new EOFException();
                }

                // The only continuation in the while loop
                continue;
              } else if (e.isExit()) {
                // usually from quit
                throw new ExitException(e.getExitStatus());
              } else if (e.isHostException()) {
                // we continue the repl even though the system may be broken
                lastStatus = 1;
                System.out.println(e.getMessage());
              } else if (e.isInternalError()) {
                System.err.println(ConsoleColor.Red.colorize("An internal error occurred:"));
                e.printStackTrace();

                // we continue the repl even though the system may be broken
                lastStatus = 1;
              } else if (e.isGuestException()) {
                e.printStackTrace();
                // drop through to continue REPL and remember last eval was an error
                lastStatus = 1;
              } else if (!e.isInternalError()) {
                printStackTrace(e);
                lastStatus = 1;
              } else {
                e.printStackTrace();
              }
            }
            break;
          }
        } catch (EOFException e) {
          System.out.println();
          return lastStatus;
        } catch (UserInterruptException e) {
          // interrupted by ctrl-c
        }
      }
    } catch (IOException e) {
      e.printStackTrace();
      return 1;
    } catch (ExitException e) {
      return e.code;
    } finally {
      consoleHandler.saveHistory();
    }
  }

  private Value evalInternal(Context context, String code) {
    return context.eval(Source.newBuilder(getLanguageId(), code, "<internal>").internal(true).buildLiteral());
  }

  private static final class ExitException extends RuntimeException {
    @Serial
    private static final long serialVersionUID = 1L;
    private final int code;

    ExitException(int code) {
      this.code = code;
    }
  }

  private static void printFileNotFoundException(NoSuchFileException e) {
    String reason = e.getReason();
    if (reason == null) {
      reason = "No such file or directory";
    }
    System.err.println(Launcher.class.getCanonicalName() + ": can't open file '" + e.getFile() + "': " + reason);
  }

  private enum State {
    NORMAL,
    SINGLE_QUOTE,
    DOUBLE_QUOTE,
    ESCAPE_SINGLE_QUOTE,
    ESCAPE_DOUBLE_QUOTE,
  }

  private static List<String> getDefaultEnvironmentArgs() {
    String pid;
    if (isAOT()) {
      pid = String.valueOf(ProcessProperties.getProcessID());
    } else {
      pid = ManagementFactory.getRuntimeMXBean().getName().split("@")[0];
    }
    String envArgsOpt = System.getenv("YONA_ARGS");
    ArrayList<String> envArgs = new ArrayList<>();
    State s = State.NORMAL;
    StringBuilder sb = new StringBuilder();
    if (envArgsOpt != null) {
      for (char x : envArgsOpt.toCharArray()) {
        if (s == State.NORMAL && Character.isWhitespace(x)) {
          addArgument(pid, envArgs, sb);
        } else {
          if (x == '"') {
            if (s == State.NORMAL) {
              s = State.DOUBLE_QUOTE;
            } else if (s == State.DOUBLE_QUOTE) {
              s = State.NORMAL;
            } else if (s == State.ESCAPE_DOUBLE_QUOTE) {
              s = State.DOUBLE_QUOTE;
              sb.append(x);
            }
          } else if (x == '\'') {
            if (s == State.NORMAL) {
              s = State.SINGLE_QUOTE;
            } else if (s == State.SINGLE_QUOTE) {
              s = State.NORMAL;
            } else if (s == State.ESCAPE_SINGLE_QUOTE) {
              s = State.SINGLE_QUOTE;
              sb.append(x);
            }
          } else if (x == '\\') {
            if (s == State.SINGLE_QUOTE) {
              s = State.ESCAPE_SINGLE_QUOTE;
            } else if (s == State.DOUBLE_QUOTE) {
              s = State.ESCAPE_DOUBLE_QUOTE;
            }
          } else {
            sb.append(x);
          }
        }
      }
      addArgument(pid, envArgs, sb);
    }
    return envArgs;
  }

  private static void addArgument(String pid, ArrayList<String> envArgs, StringBuilder sb) {
    if (sb.length() > 0) {
      String arg = sb.toString().replace("$$", pid).replace("\\$", "$");
      envArgs.add(arg);
      sb.setLength(0);
    }
  }

  private void addRelaunchArg(String arg) {
    if (relaunchArgs == null) {
      relaunchArgs = new ArrayList<>();
    }
    relaunchArgs.add(arg);
  }

  private String[] execListWithRelaunchArgs(String executableName) {
    if (relaunchArgs == null) {
      return new String[]{executableName};
    } else {
      ArrayList<String> execList = new ArrayList<>(relaunchArgs.size() + 1);
      execList.add(executableName);
      execList.addAll(relaunchArgs);
      return execList.toArray(new String[0]);
    }
  }

  /**
   * Some system properties have already been read at this point, so to change them, we just
   * re-execute the process with the additional options.
   */
  private static void subExec(List<String> args, List<String> subProcessDefs) {
    List<String> cmd = getCmdline(args, subProcessDefs);
    try {
      System.exit(new ProcessBuilder(cmd.toArray(new String[0])).inheritIO().start().waitFor());
    } catch (IOException | InterruptedException e) {
      Thread.currentThread().interrupt();
      System.err.println(e.getMessage());
      System.exit(-1);
    }
  }

  private static List<String> getCmdline(List<String> args, List<String> subProcessDefs) {
    List<String> cmd = new ArrayList<>();
    if (isAOT()) {
      cmd.add(ProcessProperties.getExecutableName());
      for (String subProcArg : subProcessDefs) {
        assert subProcArg.startsWith("D");
        cmd.add("--native." + subProcArg);
      }
    } else {
      cmd.add(System.getProperty("java.home") + File.separator + "bin" + File.separator + "java");
      switch (System.getProperty("java.vm.name")) {
        case "Java HotSpot(TM) 64-Bit Server VM":
          cmd.add("-server");
          break;
        case "Java HotSpot(TM) 64-Bit Client VM":
          cmd.add("-client");
          break;
        default:
          break;
      }
      cmd.addAll(ManagementFactory.getRuntimeMXBean().getInputArguments());
      cmd.add("-cp");
      cmd.add(ManagementFactory.getRuntimeMXBean().getClassPath());
      for (String subProcArg : subProcessDefs) {
        assert subProcArg.startsWith("D") || subProcArg.startsWith("X");
        cmd.add("-" + subProcArg);
      }
      cmd.add(Launcher.class.getName());
    }

    cmd.addAll(args);
    return cmd;
  }

  private String getContextOptionIfSetViaCommandLine(String key) {
    if (System.getProperty("polyglot." + key) != null) {
      return System.getProperty("polyglot." + key);
    }
    for (String f : givenArguments) {
      if (f.startsWith("--" + key)) {
        String[] splits = f.split("=", 2);
        if (splits.length > 1) {
          return splits[1];
        } else {
          return "true";
        }
      }
    }
    return null;
  }

  private String[] getExecutableList() {
    if (ImageInfo.inImageCode()) {
      return execListWithRelaunchArgs(ProcessProperties.getExecutableName());
    } else {
      if (BASH_LAUNCHER_EXEC_NAME != null) {
        return execListWithRelaunchArgs(BASH_LAUNCHER_EXEC_NAME);
      }
      StringBuilder sb = new StringBuilder();
      ArrayList<String> exec_list = new ArrayList<>();
      sb.append(System.getProperty("java.home")).append(File.separator).append("bin").append(File.separator).append("java");
      exec_list.add(sb.toString());
      String javaOptions = System.getenv("_JAVA_OPTIONS");
      String javaToolOptions = System.getenv("JAVA_TOOL_OPTIONS");
      for (String arg : ManagementFactory.getRuntimeMXBean().getInputArguments()) {
        if (arg.matches("-Xrunjdwp:transport=dt_socket,server=y,address=\\d+,suspend=y")) {
          arg = arg.replace("suspend=y", "suspend=n");
        }
        if ((javaOptions != null && javaOptions.contains(arg)) || (javaToolOptions != null && javaToolOptions.contains(arg))) {
          // both _JAVA_OPTIONS and JAVA_TOOL_OPTIONS are adeed during
          // JVM startup automatically. We do not want to repeat these
          // for subprocesses, because they should also pick up those
          // variables.
          continue;
        }
        exec_list.add(arg);
      }
      exec_list.add("-classpath");
      exec_list.add(System.getProperty("java.class.path"));
      exec_list.add(Launcher.class.getName());
      if (relaunchArgs != null) {
        exec_list.addAll(relaunchArgs);
      }
      return exec_list.toArray(new String[0]);
    }
  }

  private String getExecutable() {
    if (ImageInfo.inImageBuildtimeCode()) {
      return "";
    } else {
      if (BASH_LAUNCHER_EXEC_NAME != null) {
        return BASH_LAUNCHER_EXEC_NAME;
      }
      String[] executableList = getExecutableList();
      for (int i = 0; i < executableList.length; i++) {
        if (executableList[i].matches("\\s")) {
          executableList[i] = "'" + executableList[i].replace("'", "\\'") + "'";
        }
      }
      return String.join(" ", executableList);
    }
  }

  private static void printStackTrace(PolyglotException e) {
    System.err.println(ConsoleColor.Red.colorize(e.getMessage()));

    ArrayList<String> stack = new ArrayList<>();
    for (PolyglotException.StackFrame frame : e.getPolyglotStackTrace()) {
      if (frame.isGuestFrame()) {
        StringBuilder sb = new StringBuilder();
        SourceSection sourceSection = frame.getSourceLocation();
        String rootName = frame.getRootName();
        if (sourceSection != null) {
          sb.append("  ");
          String path = sourceSection.getSource().getPath();
          if (path != null) {
            sb.append("File ");
          }
          sb.append('"');
          if (sourceSection.getSource() != null) {
            sb.append(sourceSection.getSource().getName());
            sb.append("\", line ");
            sb.append(sourceSection.getStartLine());
            sb.append(", in ");
            sb.append(rootName);
          }
          stack.add(sb.toString());
        }
      }
    }
    System.err.println(ConsoleColor.White.colorize("Traceback (most recent call last):"));
    stack.forEach(System.err::println);
  }
}
