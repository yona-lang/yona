package abzu;

import org.graalvm.polyglot.Context;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

import static org.junit.jupiter.api.Assertions.*;

public class StdLibTest {
  private Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.create();
  }

  @AfterEach
  public void dispose() {
    context.close();
  }

  @Test
  public void sequenceFoldLeftTest() {
    long ret = context.eval("abzu", "sfoldl [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void sequenceFoldRightTest() {
    long ret = context.eval("abzu", "sfoldr [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void sequenceFoldLeftWithinLetTest() {
    long ret = context.eval("abzu", "let xx = 5 in sfoldl [1, 2, 3] (\\acc val -> acc + val + xx) 0").asLong();
    assertEquals(21l, ret);
  }
}
