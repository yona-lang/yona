package abzu;

import org.graalvm.polyglot.Context;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.*;

public class AbzuSimpleExpressionTest {

  private Context context;

  @Before
  public void initEngine() throws Exception {
    context = Context.create();
  }

  @After
  public void dispose() {
    context.close();
  }

  @Test
  public void numberValueTest() throws Exception {
    int ret = context.eval("abzu", "5").asInt();
    assertEquals(5, ret);
  }

  @Test
  public void unitValueTest() throws Exception {
    assertEquals("NONE", context.eval("abzu", "()").toString());
  }

  /*@Test
  public void tupleValueTest() throws Exception {
    List ret = context.eval("abzu", "(1, 2, 3)").as(List.class);
    assertEquals(Arrays.asList(1l, 2l, 3l), ret);
  }*/
}
