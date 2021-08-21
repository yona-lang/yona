package yona;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Source;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.TestFactory;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class YonaTest {
  private static final String SOURCE_SUFFIX = ".yona";
  private static final String INPUT_SUFFIX = ".input";
  private static final String OUTPUT_SUFFIX = ".output";
  private static final String TESTS_DIRECTORY = "tests";

  private static final boolean REPORT_STACKTRACE = false;

  private static String readAllLines(Path file) throws IOException {
    return Files.readString(file, StandardCharsets.UTF_8);
  }

  @TestFactory
  protected List<DynamicTest> tests() throws IOException {
    List<DynamicTest> foundCases = new ArrayList<>();
    final Path rootPath = Paths.get(TESTS_DIRECTORY);

    Files.walkFileTree(rootPath, new SimpleFileVisitor<>() {
      @Override
      public FileVisitResult visitFile(Path sourceFile, BasicFileAttributes attrs) {
        String sourceName = sourceFile.getFileName().toString();
        if (sourceName.endsWith(SOURCE_SUFFIX)) {
          String baseName = sourceName.substring(0, sourceName.length() - SOURCE_SUFFIX.length());

          DynamicTest dynamicTest = DynamicTest.dynamicTest(baseName, sourceFile.toUri(), () -> {
            Path inputFile = sourceFile.resolveSibling(baseName + INPUT_SUFFIX);
            String testInput = "";
            if (Files.exists(inputFile)) {
              testInput = readAllLines(inputFile);
            }

            Path outputFile = sourceFile.resolveSibling(baseName + OUTPUT_SUFFIX);
            String expectedOutput = "";
            if (Files.exists(outputFile)) {
              expectedOutput = readAllLines(outputFile);
            }

            ByteArrayOutputStream out = new ByteArrayOutputStream();
            PrintWriter printer = new PrintWriter(out);
            Context context = Context.newBuilder(YonaLanguage.ID)
                .in(new ByteArrayInputStream(testInput.getBytes(StandardCharsets.UTF_8)))
                .out(out)
                .err(out)
                .allowAllAccess(true)
                .environment("YONA_STDLIB_HOME", "lib-yona")
                .environment("YONA_PRINT_ALL_RESULTS", "true")
//                .option("log.yona.level", "FINE")
                .build();

            /* Parse the Yona source file. */
            Source source = Source.newBuilder(YonaLanguage.ID, sourceFile.toFile()).interactive(true).build();

            try {
              /* Call the main entry point, without any arguments. */
              System.out.println("Running " + sourceName);
              context.eval(source);
//              System.out.println("Finished " + sourceName);
            } catch (PolyglotException ex) {
              if (!ex.isInternalError()) {
                printer.println(YonaException.prettyPrint(ex.getMessage(), ex.getSourceLocation()));
                if (REPORT_STACKTRACE) {
                  if (ex.getGuestObject() != null) {
                    Object[] yonaExceptionTuple = ex.getGuestObject().as(Object[].class);
                    printer.println(yonaExceptionTuple[0] + ": " + yonaExceptionTuple[1]);

                    List<?> stackTrace = (List<?>) yonaExceptionTuple[2];
                    for (Object line : stackTrace) {
                      printer.println(line);
                    }
                  } else {
                    ex.printStackTrace(printer);
                  }
                }
              } else {
                ex.printStackTrace(printer);
              }
            }

            printer.flush();
            String actualOutput = out.toString();

            assertEquals(expectedOutput.replace("\r", "").strip(), actualOutput.replace("\r", "").strip(), sourceName);
          });

          foundCases.add(dynamicTest);
        }
        return FileVisitResult.CONTINUE;
      }
    });
    return foundCases;
  }
}
