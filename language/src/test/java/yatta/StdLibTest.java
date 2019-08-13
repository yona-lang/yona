package yatta;

import org.graalvm.polyglot.Context;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

import static org.junit.jupiter.api.Assertions.*;

public class StdLibTest {
  private Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.newBuilder().allowAllAccess(true).build();
  }

  @AfterEach
  public void dispose() {
    context.close();
  }

  @Test
  public void sequenceFoldLeftTest() {
    long ret = context.eval(YattaLanguage.ID, "Sequence.foldl [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void sequenceFoldRightTest() {
    long ret = context.eval(YattaLanguage.ID, "Sequence.foldr [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void sequenceFoldLeftWithinLetTest() {
    long ret = context.eval(YattaLanguage.ID, "let xx = 5 in Sequence.foldl [1, 2, 3] (\\acc val -> acc + val + xx) 0").asLong();
    assertEquals(21l, ret);
  }
}
