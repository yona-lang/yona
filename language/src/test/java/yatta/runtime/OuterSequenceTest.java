package yatta.runtime;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

import static yatta.runtime.OuterSequence.*;

public class OuterSequenceTest {

  private static final byte UTF8_1B = (byte) 0x05;
  private static final byte UTF8_2B = (byte) 0xc5;
  private static final byte UTF8_3B = (byte) 0xe5;
  private static final byte UTF8_4B = (byte) 0xf5;
  private static final byte UTF8_CC = (byte) 0x85;

  @Test
  public void testOffsetUtf8() {
    byte[] bytes;
    // leftmost, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, UTF8_1B, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 0)]);
    // leftmost, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_2B, UTF8_CC, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_2B, bytes[offsetUtf8(bytes, 0)]);
    // leftmost, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_3B, UTF8_CC, UTF8_CC, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_3B, bytes[offsetUtf8(bytes, 0)]);
    // leftmost, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, 0 };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_4B, bytes[offsetUtf8(bytes, 0)]);
    // rightmost, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_1B };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 1)]);
    // rightmost, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_2B, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_2B, bytes[offsetUtf8(bytes, 1)]);
    // rightmost, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_3B, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_3B, bytes[offsetUtf8(bytes, 1)]);
    // rightmost, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 2);
    assertEquals(UTF8_4B, bytes[offsetUtf8(bytes, 1)]);
    // mid-left, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, 0, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 1)]);
    // mid-left, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_2B, UTF8_CC, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 1)]);
    // mid-left, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_3B, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 1)]);
    // mid-left, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 1)]);
    // mid-right, U+0000 - U+007F
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, 0 };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 2)]);
    // mid-right, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, UTF8_2B, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 2)]);
    // mid-right, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, UTF8_3B, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 2)]);
    // mid-right, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, 0, 0, 0, 0, UTF8_1B, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    writeMeta(bytes, (1 << 31) | 4);
    assertEquals(UTF8_1B, bytes[offsetUtf8(bytes, 2)]);
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

  @Test
  public void testPush() {
    assertEquals("a", EMPTY.push("a").lookup(0, null));
    final byte[] bytes = fourBytes(0xa, 0xb, 0xc, 0xd);
    assertEquals((byte) 0xa, EMPTY.push(bytes).lookup(0, null));
    assertEquals((byte) 0xb, EMPTY.push(bytes).lookup(1, null));
    assertEquals((byte) 0xc, EMPTY.push(bytes).lookup(2, null));
    assertEquals((byte) 0xd, EMPTY.push(bytes).lookup(3, null));
    // TODO
  }

  @Test
  public void testInject() {
    assertEquals("a", EMPTY.inject("a").lookup(0, null));
    final byte[] bytes = fourBytes(0xa, 0xb, 0xc, 0xd);
    assertEquals((byte) 0xa, EMPTY.inject(bytes).lookup(0, null));
    assertEquals((byte) 0xb, EMPTY.inject(bytes).lookup(1, null));
    assertEquals((byte) 0xc, EMPTY.inject(bytes).lookup(2, null));
    assertEquals((byte) 0xd, EMPTY.inject(bytes).lookup(3, null));
    // TODO
  }

  @Test
  public void testFirst() {
    OuterSequence seq;
    seq = EMPTY.inject("a");
    assertEquals(seq.lookup(0, null), seq.first());
    seq = EMPTY.inject(fourBytes(0xa, 0x0, 0x0, 0x0));
    assertEquals(seq.lookup(0, null), seq.first());
    // TODO
  }

  @Test
  public void testRemoveFirst() {
    assertTrue(EMPTY.push(new Object()).pop().empty());
    final byte[] bytes = fourBytes(0xa, 0xb, 0xc, 0xd);
    assertEquals((byte) 0xb, EMPTY.push(bytes).pop().lookup(0, null));
    assertEquals((byte) 0xd, EMPTY.push(bytes).pop().lookup(2, null));
    // TODO
  }

  @Test
  public void testLast() {
    OuterSequence seq;
    seq = EMPTY.push("a");
    assertEquals(seq.lookup(0, null), seq.last());
    seq = EMPTY.push(fourBytes(0x0, 0x0, 0x0, 0xa));
    assertEquals(seq.lookup(3, null), seq.last());
    // TODO
  }

  @Test
  public void testRemoveLast() {
    assertTrue(EMPTY.push(new Object()).eject().empty());
    final byte[] bytes = fourBytes(0xa, 0xb, 0xc, 0xd);
    assertEquals((byte) 0xa, EMPTY.push(bytes).eject().lookup(0, null));
    assertEquals((byte) 0xc, EMPTY.push(bytes).eject().lookup(2, null));
    // TODO
  }

  private static byte[] fourBytes(int b0, int b1, int b2, int b3) {
    final byte[] result = new byte[8];
    writeMeta(result, 4);
    result[4] = (byte) b0;
    result[5] = (byte) b1;
    result[6] = (byte) b2;
    result[7] = (byte) b3;
    return result;
  }
}
