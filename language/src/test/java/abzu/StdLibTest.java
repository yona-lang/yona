package abzu;

import org.graalvm.polyglot.Context;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class StdLibTest {
  private Context context;

  @Before
  public void initEngine() {
    context = Context.create();
  }

  @After
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
}
