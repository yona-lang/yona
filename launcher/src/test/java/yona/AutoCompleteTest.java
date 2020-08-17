package yona;

import akovari.antlr4.autocomplete.Antlr4Completer;
import akovari.antlr4.autocomplete.CompletionResult;
import akovari.antlr4.autocomplete.ReflectionLexerAndParserFactory;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import yona.parser.YonaLexer;
import yona.parser.YonaParser;

import java.util.Arrays;
import java.util.logging.Handler;
import java.util.logging.Level;
import java.util.logging.Logger;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class AutoCompleteTest {
  private static ReflectionLexerAndParserFactory lexerAndParserFactory;

  @BeforeAll
  public static void init() {
    Logger root = Logger.getLogger("");
    root.setLevel(Level.ALL);
    for (Handler handler : root.getHandlers()) {
      handler.setLevel(Level.WARNING);
    }

    lexerAndParserFactory = new ReflectionLexerAndParserFactory(YonaLexer.class, YonaParser.class, (state) -> !Launcher.DISALLOWED_COMPLETION_STATES.contains(state));
  }

  @Test
  public void baselineTestcase() {
    assertTrue(true);
  }

  @Test
  public void suggestTupleCompletions() {
    expectSuggestions("(1", ",", ")");
  }

//  @Test
//  public void suggestFalseCompletions() {
//    expectSuggestions("fals", "e");
//  }

  @Test
  public void tokensTest() {
    expectTokens("http\\Client::post",
        new CompletionResult.InputToken("LOWERCASE_NAME", "http"),
        new CompletionResult.InputToken("'\\'", "\\"),
        new CompletionResult.InputToken("UPPERCASE_NAME", "Client"),
        new CompletionResult.InputToken("'::'", "::"),
        new CompletionResult.InputToken("LOWERCASE_NAME", "post"));
  }

  @Test
  public void incompleteModuleCallTest() {
    expectTokens("File::o",
        new CompletionResult.InputToken("UPPERCASE_NAME", "File"),
        new CompletionResult.InputToken("'::'", "::"),
        new CompletionResult.InputToken("LOWERCASE_NAME", "o"));
  }

  private void expectSuggestions(String input, String... expectedCompletions) {
    assertTrue(new Antlr4Completer(lexerAndParserFactory, input).complete().getSuggestions().containsAll(Arrays.stream(expectedCompletions).collect(Collectors.toSet())));
  }

  private void expectTokens(String input, CompletionResult.InputToken... expectedTokens) {
    assertEquals(Arrays.stream(expectedTokens).collect(Collectors.toUnmodifiableList()), new Antlr4Completer(lexerAndParserFactory, input).complete().getTokens());
  }
}
