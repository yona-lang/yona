package abzu.runtime;

import org.junit.Test;

import static abzu.runtime.Util.*;
import static org.junit.Assert.assertEquals;

public class UtilTest {

  @Test
  public void testVarIntReadWrite() {
    final byte[] bytes = new byte[5];
    testVarIntReadWrite(bytes, 0x0);
    testVarIntReadWrite(bytes, 0x7a);
    testVarIntReadWrite(bytes, 0x8a);
    testVarIntReadWrite(bytes, 0x3abc);
    testVarIntReadWrite(bytes, 0x4abc);
    testVarIntReadWrite(bytes, 0x1abcde);
    testVarIntReadWrite(bytes, 0x2abcde);
    testVarIntReadWrite(bytes, 0xfabcdef);
    testVarIntReadWrite(bytes, 0x1abcdef0);
    testVarIntReadWrite(bytes, 0x7fffffff);
  }

  private static void testVarIntReadWrite(byte[] bytes, int value) {
    varIntWrite(value, bytes, 0);
    assertEquals(value, varIntRead(bytes, 0));
  }

  @Test
  public void testVarIntLen() {
    assertEquals(1, varIntLen(0x0));
    assertEquals(1, varIntLen(0x7a));
    assertEquals(2, varIntLen(0x8a));
    assertEquals(2, varIntLen(0x3abc));
    assertEquals(3, varIntLen(0x4abc));
    assertEquals(3, varIntLen(0x1abcde));
    assertEquals(4, varIntLen(0x2abcde));
    assertEquals(4, varIntLen(0xfabcdef));
    assertEquals(5, varIntLen(0x1abcdef0));
    assertEquals(5, varIntLen(0x7fffffff));
  }
}
