package yatta.runtime;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

import static yatta.runtime.Util.*;

public class UtilTest {

  @Test
  public void testVarIntReadWrite() {
    testVarIntReadWrite(new byte[1], 0x0);
    testVarIntReadWrite(new byte[1], 0x7a);
    testVarIntReadWrite(new byte[2], 0x8a);
    testVarIntReadWrite(new byte[2], 0x3abc);
    testVarIntReadWrite(new byte[3], 0x4abc);
    testVarIntReadWrite(new byte[3], 0x1abcde);
    testVarIntReadWrite(new byte[4], 0x2abcde);
    testVarIntReadWrite(new byte[4], 0xfabcdef);
    testVarIntReadWrite(new byte[5], 0x1abcdef0);
    testVarIntReadWrite(new byte[5], 0x7fffffff);
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
