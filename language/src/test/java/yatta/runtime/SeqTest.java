package yatta.runtime;

import org.junit.Test;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;

import static org.junit.Assert.*;

public class SeqTest {
  private static final int N = 16384;

  private static final int[] CODE_POINTS = codePoints();

  private static final byte UTF8_1B = (byte) 0x05;
  private static final byte UTF8_2B = (byte) 0xc5;
  private static final byte UTF8_3B = (byte) 0xe5;
  private static final byte UTF8_4B = (byte) 0xf5;
  private static final byte UTF8_CC = (byte) 0x85;

  @Test
  public void testOffsetUtf8() {
    byte[] bytes;
    // leftmost, U+0000 - U+007F
    bytes = new byte[] { UTF8_1B, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 0, 2)]);
    // leftmost, U+0080 - U+07FF
    bytes = new byte[] { UTF8_2B, UTF8_CC, 0 };
    assertEquals(UTF8_2B, bytes[Seq.offsetUtf8(bytes, 0, 2)]);
    // leftmost, U+0800 - U+FFFF
    bytes = new byte[] { UTF8_3B, UTF8_CC, UTF8_CC, 0 };
    assertEquals(UTF8_3B, bytes[Seq.offsetUtf8(bytes, 0, 2)]);
    // leftmost, U+10000 - U+10FFFF
    bytes = new byte[] { UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, 0 };
    assertEquals(UTF8_4B, bytes[Seq.offsetUtf8(bytes, 0, 2)]);
    // rightmost, U+0000 - U+007F
    bytes = new byte[] { 0, UTF8_1B };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1, 2)]);
    // rightmost, U+0080 - U+07FF
    bytes = new byte[] { 0, UTF8_2B, UTF8_CC };
    assertEquals(UTF8_2B, bytes[Seq.offsetUtf8(bytes, 1, 2)]);
    // rightmost, U+0800 - U+FFFF
    bytes = new byte[] { 0, UTF8_3B, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_3B, bytes[Seq.offsetUtf8(bytes, 1, 2)]);
    // rightmost, U+10000 - U+10FFFF
    bytes = new byte[] { 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_4B, bytes[Seq.offsetUtf8(bytes, 1, 2)]);
    // mid-left, U+0000 - U+007F
    bytes = new byte[] { 0, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1, 4)]);
    // mid-left, U+0080 - U+07FF
    bytes = new byte[] { UTF8_2B, UTF8_CC, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1, 4)]);
    // mid-left, U+0800 - U+FFFF
    bytes = new byte[] { UTF8_3B, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1, 4)]);
    // mid-left, U+10000 - U+10FFFF
    bytes = new byte[] { UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1, 4)]);
    // mid-right, U+0000 - U+007F
    bytes = new byte[] { 0, 0, UTF8_1B, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2, 4)]);
    // mid-right, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, UTF8_1B, UTF8_2B, UTF8_CC };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2, 4)]);
    // mid-right, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, UTF8_1B, UTF8_3B, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2, 4)]);
    // mid-right, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, UTF8_1B, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2, 4)]);
  }

  @Test
  public void testInsertLast() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      seq = seq.insertLast(i);
      for (long j = 0; j < i; j++) {
        assertEquals(j, seq.lookup(j, null));
      }
    }
  }

  @Test
  public void testInsertLastEncodedBytes() {
    Seq seq = Seq.EMPTY;
    for (int i = 0; i < N; i += 128) {
      seq = seq.insertLastEncoded(bytes(), 128, Seq.EncodedType.BYTES);
      for (int j = 0; j < i; j++) {
        final byte expected = (byte) (j % 128);
        assertEquals(expected, seq.lookup(j, null));
      }
    }
  }

  private static byte[] bytes() {
    final byte[] result = new byte[128];
    for (int i = 0; i < 128; i++) {
      result[i] = (byte) i;
    }
    return result;
  }

  @Test
  public void testInsertLastEncodedUtf8() {
    Seq seq = Seq.EMPTY;
    for (int codePoint : CODE_POINTS) {
      seq = seq.insertLastEncoded(codePointToBytes(codePoint), 1, Seq.EncodedType.UTF8);
    }
    for (int i = 0; i < CODE_POINTS.length; i++) {
      final byte[] expected = codePointToBytes(CODE_POINTS[i]);
      assertArrayEquals(expected, codePointToBytes((Integer) seq.lookup(i, null)));
    }
  }

  private static int[] codePoints() {
    final ArrayList<Integer> list = new ArrayList<>();
    for (int i = 0; i < 0x10ffff; i++) {
      if (Character.isValidCodePoint(i)) list.add(i);
    }
    final int[] result = new int[list.size()];
    for (int i = 0; i < result.length; i++) result[i] = list.get(i);
    return result;
  }

  private static byte[] codePointToBytes(final int codePoint) {
    return new String(new int[]{ codePoint }, 0, 1).getBytes(StandardCharsets.UTF_8);
  }

  @Test
  public void testInsertLastMixed() {
    final Object[] data = new Object[128];
    for (int i = 0; i < data.length; i++) {
      if (i % 2 == 0) data[i] = i;
      else if (i % 3 == 0) data[i] = (byte) i;
      else data[i] = (long) i;
    }
    Seq seq = Seq.EMPTY;
    for (Object o : data) {
      if (o instanceof Integer) seq = seq.insertLastEncoded(codePointToBytes((Integer) o), 1, Seq.EncodedType.UTF8);
      else if (o instanceof Byte) seq = seq.insertLastEncoded(new byte[]{ (byte) o }, 1, Seq.EncodedType.BYTES);
      else seq = seq.insertLast(o);
    }
    for (int i = 0; i < data.length; i++) {
      Object expected = data[i];
      assertEquals(expected, seq.lookup(i, null));
    }
  }
}
