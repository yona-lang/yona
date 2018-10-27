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

//  @Test
//  public void stringValueTest() {
//    String ret = context.eval("abzu", "\"string\"").asString();
//    assertEquals("string", ret, 0);
//  }
//
//  @Test
//  public void symbolValueTest() {
//    String ret = context.eval("abzu", ":symbol").asString();
//    assertEquals("symbol", ret, 0);
//  }
//
//  @Test
//  public void tupleValueTest() throws Exception {
//    List ret = context.eval("abzu", "(1, 2, 3)").as(List.class);
//    assertEquals(Arrays.asList(1l, 2l, 3l), ret);
//  }
}
