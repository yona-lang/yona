package abzu.runtime;

import org.junit.Test;

import static abzu.runtime.Util.varIntRead;
import static abzu.runtime.Util.varIntWrite;
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
}
