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
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 0)]);
    // leftmost, U+0080 - U+07FF
    bytes = new byte[] { UTF8_2B, UTF8_CC, 0 };
    assertEquals(UTF8_2B, bytes[Seq.offsetUtf8(bytes, 0)]);
    // leftmost, U+0800 - U+FFFF
    bytes = new byte[] { UTF8_3B, UTF8_CC, UTF8_CC, 0 };
    assertEquals(UTF8_3B, bytes[Seq.offsetUtf8(bytes, 0)]);
    // leftmost, U+10000 - U+10FFFF
    bytes = new byte[] { UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, 0 };
    assertEquals(UTF8_4B, bytes[Seq.offsetUtf8(bytes, 0)]);
    // rightmost, U+0000 - U+007F
    bytes = new byte[] { 0, UTF8_1B };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // rightmost, U+0080 - U+07FF
    bytes = new byte[] { 0, UTF8_2B, UTF8_CC };
    assertEquals(UTF8_2B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // rightmost, U+0800 - U+FFFF
    bytes = new byte[] { 0, UTF8_3B, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_3B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // rightmost, U+10000 - U+10FFFF
    bytes = new byte[] { 0, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_4B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // mid-left, U+0000 - U+007F
    bytes = new byte[] { 0, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // mid-left, U+0080 - U+07FF
    bytes = new byte[] { UTF8_2B, UTF8_CC, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // mid-left, U+0800 - U+FFFF
    bytes = new byte[] { UTF8_3B, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // mid-left, U+10000 - U+10FFFF
    bytes = new byte[] { UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC, UTF8_1B, 0, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 1)]);
    // mid-right, U+0000 - U+007F
    bytes = new byte[] { 0, 0, UTF8_1B, 0 };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2)]);
    // mid-right, U+0080 - U+07FF
    bytes = new byte[] { 0, 0, UTF8_1B, UTF8_2B, UTF8_CC };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2)]);
    // mid-right, U+0800 - U+FFFF
    bytes = new byte[] { 0, 0, UTF8_1B, UTF8_3B, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2)]);
    // mid-right, U+10000 - U+10FFFF
    bytes = new byte[] { 0, 0, UTF8_1B, UTF8_4B, UTF8_CC, UTF8_CC, UTF8_CC };
    assertEquals(UTF8_1B, bytes[Seq.offsetUtf8(bytes, 2)]);
  }

  @Test
  public void testInsertFirst() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      Seq newSeq = seq.insertFirst(i);
      for (long j = 0; j < i; j++) {
        assertEquals(j, seq.lookup(seq.length() - j - 1, null));
      }
      for (long j = 0; j <= i; j++) {
        assertEquals(j, newSeq.lookup(newSeq.length() - j - 1, null));
      }
      seq = newSeq;
    }
  }

  @Test
  public void testInsertLast() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      Seq newSeq = seq.insertLast(i);
      for (long j = 0; j < i; j++) {
        assertEquals(j, seq.lookup(j, null));
      }
      for (long j = 0; j <= i; j++) {
        assertEquals(j, newSeq.lookup(j, null));
      }
      seq = newSeq;
    }
  }

  @Test
  public void testInsertFirstEncodedBytes() {
    Seq seq = Seq.EMPTY;
    for (int i = 0; i < N; i += 128) {
      Seq newSeq = seq.insertFirstEncoded(bytes(), 128, false);
      for (int j = 0; j < i; j++) {
        final byte expected = (byte) (j % 128);
        assertEquals(expected, seq.lookup(j, null));
      }
      for (int j = 0; j <= i; j++) {
        final byte expected = (byte) (j % 128);
        assertEquals(expected, newSeq.lookup(j, null));
      }
      seq = newSeq;
    }
  }

  @Test
  public void testInsertLastEncodedBytes() {
    Seq seq = Seq.EMPTY;
    for (int i = 0; i < N; i += 128) {
      Seq newSeq = seq.insertLastEncoded(bytes(), 128, false);
      for (int j = 0; j < i; j++) {
        final byte expected = (byte) (j % 128);
        assertEquals(expected, seq.lookup(j, null));
      }
      for (int j = 0; j <= i; j++) {
        final byte expected = (byte) (j % 128);
        assertEquals(expected, newSeq.lookup(j, null));
      }
      seq = newSeq;
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
      seq = seq.insertLastEncoded(codePointToBytes(codePoint), 1, true);
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
  public void testInsertFirstMixed() {
    final Object[] data = data();
    Seq seq = Seq.EMPTY;
    for (int i = data.length - 1; i >= 0; i--) {
      final Object o = data[i];
      if (o instanceof Integer) seq = seq.insertFirstEncoded(codePointToBytes((Integer) o), 1, true);
      else if (o instanceof Byte) seq = seq.insertFirstEncoded(new byte[]{ (byte) o }, 1, false);
      else seq = seq.insertFirst(o);
    }
    for (int i = 0; i < data.length; i++) {
      Object expected = data[i];
      assertEquals(expected, seq.lookup(i, null));
    }
  }

  @Test
  public void testInsertLastMixed() {
    final Object[] data = data();
    Seq seq = Seq.EMPTY;
    for (final Object o : data) {
      if (o instanceof Integer) seq = seq.insertLastEncoded(codePointToBytes((Integer) o), 1, true);
      else if (o instanceof Byte) seq = seq.insertLastEncoded(new byte[]{(byte) o}, 1, false);
      else seq = seq.insertLast(o);
    }
    for (int i = 0; i < data.length; i++) {
      Object expected = data[i];
      assertEquals(expected, seq.lookup(i, null));
    }
  }

  private static Object[] data() {
    final Object[] result = new Object[128];
    for (int i = 0; i < result.length; i++) {
      if (i % 2 == 0) result[i] = i;
      else if (i % 3 == 0) result[i] = (byte) i;
      else result[i] = (long) i;
    }
    return result;
  }

  @Test
  public void testRemoveFirst() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      seq = seq.insertFirst(i);
    }
    while (seq.length() != 0) {
      Seq newSeq = seq.removeFirst();
      for (long j = 0; j < seq.length(); j++) {
        assertEquals(j, seq.lookup(seq.length() - j - 1, null));
      }
      for (long j = 0; j < newSeq.length(); j++) {
        assertEquals(j, newSeq.lookup(newSeq.length() - j - 1, null));
      }
      seq = newSeq;
    }
  }

  @Test
  public void testRemoveLast() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      seq = seq.insertLast(i);
    }
    while (seq.length() != 0) {
      Seq newSeq = seq.removeLast();
      for (long j = 0; j < seq.length(); j++) {
        assertEquals(j, seq.lookup(j, null));
      }
      for (long j = 0; j < newSeq.length(); j++) {
        assertEquals(j, newSeq.lookup(j, null));
      }
      seq = newSeq;
    }
  }

  @Test
  public void testRemoveFirstEncodedBytes() {
    Seq seq = Seq.EMPTY;
    for (int i = 0; i < N; i += 128) {
      seq = seq.insertLastEncoded(bytes(), 128, false);
    }
    for (int j = 0; j < N; j++) {
      final byte expected = (byte) (j % 128);
      assertEquals(expected, seq.lookup(0, null));
      seq = seq.removeFirst();
    }
  }

  @Test
  public void testRemoveLastEncodedBytes() {
    Seq seq = Seq.EMPTY;
    for (int i = 0; i < N; i += 128) {
      seq = seq.insertFirstEncoded(bytes(), 128, false);
    }
    for (int j = 0; j < N; j++) {
      final byte expected = (byte) (127 - (byte) (j % 128));
      assertEquals(expected, seq.lookup(seq.length() - 1, null));
      seq = seq.removeLast();
    }
  }

  @Test
  public void testRemoveFirstEncodedUtf8() {
    Seq seq = Seq.EMPTY;
    for (int codePoint : CODE_POINTS) {
      seq = seq.insertLastEncoded(codePointToBytes(codePoint), 1, true);
    }
    for (int codePoint : CODE_POINTS) {
      final byte[] expected = codePointToBytes(codePoint);
      assertArrayEquals(expected, codePointToBytes((Integer) seq.lookup(0, null)));
      seq = seq.removeFirst();
    }
  }

  @Test
  public void testRemoveLastEncodedUtf8() {
    Seq seq = Seq.EMPTY;
    for (int codePoint : CODE_POINTS) {
      seq = seq.insertFirstEncoded(codePointToBytes(codePoint), 1, true);
    }
    for (int codePoint : CODE_POINTS) {
      final byte[] expected = codePointToBytes(codePoint);
      assertArrayEquals(expected, codePointToBytes((Integer) seq.lookup(seq.length() - 1, null)));
      seq = seq.removeLast();
    }
  }

  @Test
  public void testCatenate5() {
    testCatenate(5);
  }

  @Test
  public void testCatenate50() {
    testCatenate(50);
  }

  @Test
  public void testCatenate500() {
    testCatenate(500);
  }

  private void testCatenate(final long n) {
    Seq left = Seq.EMPTY;
    Seq right = Seq.EMPTY;
    for (long i = 0; i < n; i++) {
      if (i < n/2) {
        left = left.insertLast(i);
      } else {
        right = right.insertLast(i);
      }
    }
    final Seq seq = Seq.catenate(left, right);
    for (long i = 0; i < n; i++) {
      assertEquals(i, seq.lookup(i, null));
    }
  }

  @Test
  public void testCatenateBytes() {
    Seq left = Seq.EMPTY;
    Seq right = Seq.EMPTY;
    for (int i = 0; i < N; i += 128) {
      if (i < N/2) {
        left = left.insertLastEncoded(bytes(), 128, false);
      } else {
        right = right.insertLastEncoded(bytes(), 128, false);
      }
    }
    final Seq seq = Seq.catenate(left, right);
    for (int j = 0; j < N; j++) {
      final byte expected = (byte) (j % 128);
      assertEquals(expected, seq.lookup(j, null));
    }
  }
}
