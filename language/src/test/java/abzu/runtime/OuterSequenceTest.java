package abzu.runtime;

import org.junit.Test;

import static abzu.runtime.OuterSequence.*;
import static org.junit.Assert.assertEquals;

public class OuterSequenceTest {

  @Test
  public void testOffsetOf() {
    final byte UTF8_1B = (byte) 0x05;
    final byte UTF8_2B = (byte) 0xc5;
    final byte UTF8_3B = (byte) 0xe5;
    final byte UTF8_4B = (byte) 0xf5;
    final byte UTF8_CC = (byte) 0x85;
    byte[] bytes;
    // leftmost, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, UTF8_1B, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 0)]);
    // leftmost, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_2B, UTF8_CC, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_2B, bytes[offsetOf(bytes, 0)]);
    // leftmost, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_3B, UTF8_CC, UTF8_CC, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_3B, bytes[offsetOf(bytes, 0)]);
    // leftmost, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_4B, bytes[offsetOf(bytes, 0)]);
    // rightmost, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_1B };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 1)]);
    // rightmost, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_2B, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_2B, bytes[offsetOf(bytes, 1)]);
    // rightmost, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_3B, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_3B, bytes[offsetOf(bytes, 1)]);
    // rightmost, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_4B, bytes[offsetOf(bytes, 1)]);
    // mid-left, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 1)]);
    // mid-left, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_2B, UTF8_CC, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 1)]);
    // mid-left, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_3B, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 1)]);
    // mid-left, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 1)]);
    // mid-right, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 2)]);
    // mid-right, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, UTF8_2B, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 2)]);
    // mid-right, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, UTF8_3B, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 2)]);
    // mid-right, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetOf(bytes, 2)]);
  }

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
  public void testReadWriteMeta() {
    final byte[] bytes = new byte[4];
    testReadWriteMeta(bytes, 0x0);
    testReadWriteMeta(bytes, 0x7abcdef0);
    testReadWriteMeta(bytes, 0xffffffff);
  }

  private static void testReadWriteMeta(byte[] bytes, int value) {
    writeMeta(bytes, value);
    assertEquals(value, readMeta(bytes));
  }
}
