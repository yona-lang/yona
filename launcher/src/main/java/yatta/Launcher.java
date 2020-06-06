package yatta;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Source;

import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class Launcher {
  private final static String LANGUAGE_ID = "yatta";

  private Launcher() {
  }

  public static void main(String[] args) throws IOException {
    Source source;
    Map<String, String> options = new HashMap<>();
    String file = null;
    for (String arg : args) {
      if (!parseOption(options, arg) && file == null) {
        file = arg;
      }
    }

    if (file == null) {
      // @formatter:off
      source = Source.newBuilder(LANGUAGE_ID, new InputStreamReader(System.in), "<stdin>").interactive(true).build();
      // @formatter:on
    } else {
      source = Source.newBuilder(LANGUAGE_ID, new File(file)).interactive(true).build();
    }

    if (options.size() > 0) {
      System.out.println("Running with options: " + options);
    }
    System.exit(executeSource(source, options));
  }

  private static int executeSource(Source source, Map<String, String> options) {
    Context context;
    try {
      context = Context.newBuilder(LANGUAGE_ID).in(System.in).out(System.out).options(options).allowAllAccess(true).build();
    } catch (IllegalArgumentException e) {
      System.err.println(e.getMessage());
      return 1;
    }

    try {
      context.eval(source);
      return 0;
    } catch (PolyglotException ex) {
      if (!ex.isInternalError()) {
        System.err.println(prettyPrintException(ex.getMessage(), ex.getSourceLocation()));
        if (ex.getGuestObject() != null) {
          Object[] yattaExceptionTuple = ex.getGuestObject().as(Object[].class);
          System.err.println(yattaExceptionTuple[0] + ": " + yattaExceptionTuple[1]);

          List stackTrace = (List) yattaExceptionTuple[2];
          for (Object line : stackTrace) {
            System.err.println(line);
          }
        } else {
          ex.printStackTrace(System.err);
        }
      }
      return ex.getExitStatus();
    } finally {
      try {
        context.eval(Source.newBuilder("yatta", "shutdown", "shutdown").internal(true).build());
      } catch (IOException e) {
      } finally {
        context.close();
      }
    }
  }

  private static String prettyPrintException(String message, org.graalvm.polyglot.SourceSection sourceLocation) {
    StringBuilder sb = new StringBuilder();
    sb.append(message);
    if (sourceLocation != null) {
      if (sourceLocation.getSource() != null) {
        sb.append(" at ");
        sb.append(sourceLocation.getSource().getName());
        sb.append(":");
      }
      sb.append("\n");
      sb.append(sourceLocation.getCharacters());
    }
    return sb.toString();
  }

  private static boolean parseOption(Map<String, String> options, String arg) {
    if (arg.length() <= 2 || !arg.startsWith("--")) {
      return false;
    }
    int eqIdx = arg.indexOf('=');
    String key;
    String value;
    if (eqIdx < 0) {
      key = arg.substring(2);
      value = null;
    } else {
      key = arg.substring(2, eqIdx);
      value = arg.substring(eqIdx + 1);
    }

    if (value == null) {
      value = "true";
    }
    int index = key.indexOf('.');
    String group = key;
    if (index >= 0) {
      group = group.substring(0, index);
    }
    options.put(key, value);
    return true;
  }
}
