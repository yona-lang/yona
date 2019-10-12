package yatta;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Source;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.TestFactory;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.charset.Charset;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class YattaTest {
  private static final String SOURCE_SUFFIX = ".yatta";
  private static final String INPUT_SUFFIX = ".input";
  private static final String OUTPUT_SUFFIX = ".output";
  private static final String TESTS_DIRECTORY = "tests";

  private static final String LF = System.getProperty("line.separator");

  @TestFactory
  protected List<DynamicTest> tests() throws IOException {
    List<DynamicTest> foundCases = new ArrayList<>();
    final Path rootPath = Paths.get(TESTS_DIRECTORY);

    Files.walkFileTree(rootPath, new SimpleFileVisitor<Path>() {
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
            Context context = Context.newBuilder().in(new ByteArrayInputStream(testInput.getBytes("UTF-8"))).out(out).allowAllAccess(true).build();

            /* Parse the Yatta source file. */
            Source source = Source.newBuilder(YattaLanguage.ID, sourceFile.toFile()).interactive(true).build();

            try {
              /* Call the main entry point, without any arguments. */
              context.eval(source);
            } catch (PolyglotException ex) {
              if (!ex.isInternalError()) {
                printer.println(ex.getMessage());
              } else {
                throw ex;
              }
            }

            printer.flush();
            String actualOutput = new String(out.toByteArray());

            assertEquals(expectedOutput, actualOutput, sourceName);

            context.close();
          });

          if(baseName.equals("fread"))
          foundCases.add(dynamicTest);
        }
        return FileVisitResult.CONTINUE;
      }
    });
    return foundCases;
  }

  private static String readAllLines(Path file) throws IOException {
    // fix line feeds for non unix os
    StringBuilder outFile = new StringBuilder();
    for (String line : Files.readAllLines(file, Charset.defaultCharset())) {
      outFile.append(line).append(LF);
    }
    return outFile.toString();
  }
}
