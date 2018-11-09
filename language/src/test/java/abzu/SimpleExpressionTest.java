package abzu;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.Value;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class SimpleExpressionTest {

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
  public void longValueTest() {
    long ret = context.eval("abzu", "5").asLong();
    assertEquals(5l, ret);
  }

  @Test
  public void byteValueTest() {
    byte ret = context.eval("abzu", "5b").asByte();
    assertEquals(5, ret);
  }

  @Test
  public void floatValueTest() {
    double ret = context.eval("abzu", "5.0").asDouble();
    assertEquals(5.0, ret, 0);
  }

  @Test
  public void unitValueTest() {
    assertEquals("NONE", context.eval("abzu", "()").toString());
  }

  @Test
  public void stringValueTest() {
    String ret = context.eval("abzu", "\"abzu-string\"").asString();
    assertEquals("abzu-string", ret);
  }

  @Test
  public void symbolValueTest() {
    String ret = context.eval("abzu", ":abzuSymbol").asString();
    assertEquals("abzuSymbol", ret);
  }

  @Test
  public void tupleValueTest() {
    Value tuple = context.eval("abzu", "(1, 2, 3)");
    assertEquals(3, tuple.getArraySize());

    Object[] array = tuple.as(Object[].class);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
    assertEquals(3l, array[2]);
  }
}
