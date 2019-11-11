package yatta.runtime;


import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.util.*;

import static org.junit.Assert.*;
import static yatta.runtime.Seq.*;

public class SeqTest {

  private static final int N = 262209;
  private static final int M = 4161;

  private static final byte[] BYTES = bytes();
  private static final int[] CODE_POINTS = codePoints();

  private static byte[] bytes() {
    final byte[] result = new byte[256];
    byte b = Byte.MIN_VALUE;
    for (int i = 0; i < result.length; i++) {
      result[i] = b++;
    }
    return result;
  }

  private static int[] codePoints() {
    //noinspection MismatchedQueryAndUpdateOfStringBuilder
    StringBuilder builder = new StringBuilder();
    for (int i = 0; i < Integer.MAX_VALUE; i++) {
      if (Util.utf8Length(i) != -1) {
        builder.appendCodePoint(i);
      }
    }
    return builder.codePoints().toArray();
  }

  @Test
  public void testNewMetaWithFirst() {
    assertArrayEquals(new long[]{ 2, 5, 8, 11, 14 }, newMetaWithFirst(2, 3, 4));
    assertArrayEquals(new long[]{ 5 }, newMetaWithFirst(5, 3, 0));
  }

  @Test
  public void testNewMetaWithLast() {
    assertArrayEquals(new long[]{ 3, 6, 9, 12, 14 }, newMetaWithLast(3, 4, 2));
    assertArrayEquals(new long[]{ 5 }, newMetaWithLast(3, 0, 5));
  }

  @Test
  public void testMetaInsertFirst() {
    long[] oldMeta = new long[]{ 2 };
    long[] newMeta = metaInsertFirst(oldMeta, 5);
    assertArrayEquals(new long[]{ 2 }, oldMeta);
    assertArrayEquals(new long[]{ 5, 7 }, newMeta);
    oldMeta = newMeta;
    newMeta = metaInsertFirst(oldMeta, 4);
    assertArrayEquals(new long[]{ 5, 7 }, oldMeta);
    assertArrayEquals(new long[]{ 4, 9, 11 }, newMeta);
  }

  @Test
  public void testMetaInsertLast() {
    long[] oldMeta = new long[]{ 2 };
    long[] newMeta = metaInsertLast(oldMeta, 5);
    assertArrayEquals(new long[]{ 2 }, oldMeta);
    assertArrayEquals(new long[]{ 2, 7 }, newMeta);
    oldMeta = newMeta;
    newMeta = metaInsertLast(oldMeta, 4);
    assertArrayEquals(new long[]{ 2, 7 }, oldMeta);
    assertArrayEquals(new long[]{ 2, 7, 11 }, newMeta);
  }

  @Test
  public void testMetaReplaceFirst() {
    long[] oldMeta = new long[]{ 2 };
    long[] newMeta = metaReplaceFirst(oldMeta, 3);
    assertArrayEquals(new long[]{ 2 }, oldMeta);
    assertArrayEquals(new long[]{ 3 }, newMeta);
    oldMeta = new long[]{ 1, 5 };
    newMeta = metaReplaceFirst(oldMeta, 2);
    assertArrayEquals(new long[]{ 1, 5 }, oldMeta);
    assertArrayEquals(new long[]{ 2, 6}, newMeta);
  }

  @Test
  public void testMetaReplaceLast() {
    long[] oldMeta = new long[]{ 2 };
    long[] newMeta = metaReplaceLast(oldMeta, 3);
    assertArrayEquals(new long[]{ 2 }, oldMeta);
    assertArrayEquals(new long[]{ 3 }, newMeta);
    oldMeta = new long[]{ 4, 5 };
    newMeta = metaReplaceLast(oldMeta, 2);
    assertArrayEquals(new long[]{ 4, 5 }, oldMeta);
    assertArrayEquals(new long[]{ 4, 6}, newMeta);
  }

  @Test
  public void testMetaRemoveFirst() {
    assertNull(metaRemoveFirst(new long[]{ 11 }));
    assertArrayEquals(new long[]{ 5 }, metaRemoveFirst(new long[]{ 4, 9 }));
    assertArrayEquals(new long[]{ 2, 4 }, metaRemoveFirst(new long[]{ 1, 3, 5 }));
  }

  @Test
  public void testMetaRemoveLast() {
    assertNull(metaRemoveLast(new long[]{ 11 }));
    assertArrayEquals(new long[]{ 4 }, metaRemoveLast(new long[]{ 4, 9 }));
    assertArrayEquals(new long[]{ 2, 4 }, metaRemoveLast(new long[]{ 2, 4, 5 }));
  }

  @Test
  public void testMetaCanNullifyTail() {
    long[] meta = new long[]{ 3, 6, 9 };
    assertTrue(metaCanNullifyTail(meta, 3));
    meta = new long[]{ 1, 4 };
    assertTrue(metaCanNullifyTail(meta, 3));
    meta = new long[]{ 69 };
    assertTrue(metaCanNullifyTail(meta, 3));
    meta = new long[]{ 3, 5, 8 };
    assertFalse(metaCanNullifyTail(meta, 3));
  }

  @Test
  public void testMetaCanNullifyInit() {
    long[] meta = new long[]{ 3, 6, 9 };
    assertTrue(metaCanNullifyInit(meta, 3));
    meta = new long[]{ 3, 5 };
    assertTrue(metaCanNullifyInit(meta, 3));
    meta = new long[]{ 69 };
    assertTrue(metaCanNullifyInit(meta, 3));
    meta = new long[]{ 3, 5, 8 };
    assertFalse(metaCanNullifyInit(meta, 3));
  }

  @Test
  public void testNodeLength() {
    assertEquals(4, nodeLength(new byte[]{ encode(4, true), 0x0, 0x0, 0x0, 0x0 }));
    assertEquals(4, nodeLength(new Object[5]));
  }

  @Test
  public void testNodeMeta() {
    assertNull(nodeMeta(new byte[2]));
    assertNull(nodeMeta(new Object[2]));
    assertArrayEquals(new long[]{ 1 }, nodeMeta(new Object[]{ new long[]{ 1 }, new Object()}));
  }

  @Test
  public void testNodeIsSpecial() {
    Object node = new Object[65];
    assertFalse(nodeIsSpecial(node));
    ((Object[]) node)[0] = new long[64];
    assertTrue(nodeIsSpecial(node));
    node = new Object[64];
    assertTrue(nodeIsSpecial(node));
    node = new byte[65];
    ((byte[]) node)[0] = encode(64, false);
    assertFalse(nodeIsSpecial(node));
    node = new byte[64];
    ((byte[]) node)[0] = encode(63, true);
    assertTrue(nodeIsSpecial(node));
  }

  @Test
  public void testNodeSize() {
    Object node = new Object[65];
    assertEquals(64, nodeSize(node, 0));
    assertEquals(4096, nodeSize(node, 6));
    long[] meta = new long[64];
    meta[63] = 115;
    ((Object[]) node)[0] = meta;
    assertEquals(115, nodeSize(node, 0));
    assertEquals(115, nodeSize(node, 6));
    node = new byte[34];
    ((byte[]) node)[0] = encode(33, false);
    assertEquals(33, nodeSize(node, 0));
  }

  @Test
  public void testNodeLookup() {
    Object node = new Object[]{ null, "aaa" };
    assertEquals("aaa", nodeLookup(node, 0));
    node = new byte[]{ encode(1, false), 1 };
    assertEquals((byte) 1, nodeLookup(node, 0));
    node = new byte[]{ encode(1, true), 1 };
    assertEquals(1, nodeLookup(node, 0));
  }

  @Test
  public void testLeafInsertFirst() {
    Object[] oldLeaf = new Object[]{ null };
    Object[] newLeaf = leafInsertFirst(oldLeaf, "a");
    assertArrayEquals(new Object[]{ null }, oldLeaf);
    assertArrayEquals(new Object[]{ null, "a" }, newLeaf);
    oldLeaf = newLeaf;
    newLeaf = leafInsertFirst(oldLeaf, "b");
    assertArrayEquals(new Object[]{ null, "a" }, oldLeaf);
    assertArrayEquals(new Object[]{ null, "b", "a" }, newLeaf);
  }

  @Test
  public void testLeafInsertLast() {
    Object[] oldLeaf = new Object[]{ null };
    Object[] newLeaf = leafInsertLast(oldLeaf, "a");
    assertArrayEquals(new Object[]{ null }, oldLeaf);
    assertArrayEquals(new Object[]{ null, "a" }, newLeaf);
    oldLeaf = newLeaf;
    newLeaf = leafInsertLast(oldLeaf, "b");
    assertArrayEquals(new Object[]{ null, "a" }, oldLeaf);
    assertArrayEquals(new Object[]{ null, "a", "b" }, newLeaf);
  }

  @Test
  public void testLeafRemoveFirst() {
    Object[] oldLeaf = new Object[]{ null, "a", "b" };
    Object[] newLeaf = leafRemoveFirst(oldLeaf);
    assertArrayEquals(new Object[]{ null, "a", "b" }, oldLeaf);
    assertArrayEquals(new Object[]{ null, "b" }, newLeaf);
    oldLeaf = newLeaf;
    newLeaf = leafRemoveFirst(oldLeaf);
    assertArrayEquals(new Object[]{ null, "b" }, oldLeaf);
    assertArrayEquals(new Object[]{ null }, newLeaf);
  }

  @Test
  public void testLeafRemoveLast() {
    Object[] oldLeaf = new Object[]{ null, "a", "b" };
    Object[] newLeaf = leafRemoveLast(oldLeaf);
    assertArrayEquals(new Object[]{ null, "a", "b" }, oldLeaf);
    assertArrayEquals(new Object[]{ null, "a" }, newLeaf);
    oldLeaf = newLeaf;
    newLeaf = leafRemoveLast(oldLeaf);
    assertArrayEquals(new Object[]{ null, "a" }, oldLeaf);
    assertArrayEquals(new Object[]{ null }, newLeaf);
  }

  @Test
  public void testNewNonLeaf() {
    Object[] node = newNonLeaf(standardNode(), 6);
    assertNull(nodeMeta(node));
    assertArrayEquals(standardNode(), (Object[]) node[1]);
    node = newNonLeaf(specialNode(), 6);
    assertArrayEquals(new long[]{ 1000 }, nodeMeta(node));
    assertArrayEquals(specialNode(), (Object[]) node[1]);
    node = newNonLeaf(standardNode(), specialNode(), 6);
    assertArrayEquals(new long[]{ 4096, 5096 }, nodeMeta(node));
    assertArrayEquals(standardNode(), (Object[]) node[1]);
    assertArrayEquals(specialNode(), (Object[]) node[2]);
    node = newNonLeaf(standardNode(), standardNode(), 6);
    assertNull(node[0]);
    node = newNonLeaf(specialNode(), specialNode(), 6);
    assertArrayEquals(new long[]{ 1000, 2000 }, nodeMeta(node));
    node = newNonLeaf(specialNode(), standardNode(), 6);
    assertArrayEquals(new long[]{ 1000, 5096 }, nodeMeta(node));
    assertArrayEquals(specialNode(), (Object[]) node[1]);
    assertArrayEquals(standardNode(), (Object[]) node[2]);
    node = newNonLeaf(standardNode(), specialNode(), 6);
    assertArrayEquals(new long[]{ 4096, 5096 }, nodeMeta(node));
    assertArrayEquals(standardNode(), (Object[]) node[1]);
    assertArrayEquals(specialNode(), (Object[]) node[2]);
  }

  private static Object[] standardNode() {
    return new Object[65];
  }

  private static Object[] specialNode() {
    final Object[] result = new Object[65];
    final long[] meta = new long[64];
    meta[63] = 1000;
    result[0] = meta;
    return result;
  }

  @Test
  public void testNonLeafInsertFirst() {
    Object[] oldNode = new Object[]{ null };
    Object[] newNode = nonLeafInsertFirst(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ null }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafInsertFirst(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafInsertFirst(oldNode, specialNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 1000, 5096, 9192 }, specialNode(), standardNode(), standardNode() }, newNode);
  }

  @Test
  public void testNonLeafInsertLast() {
    Object[] oldNode = new Object[]{ null };
    Object[] newNode = nonLeafInsertLast(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ null }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafInsertLast(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafInsertLast(oldNode, specialNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 4096, 8192, 9192 }, standardNode(), standardNode(), specialNode() }, newNode);
  }

  @Test
  public void testNonLeafReplaceFirst() {
    Object[] oldNode = new Object[]{ null, standardNode() };
    Object[] newNode = nonLeafReplaceFirst(oldNode, specialNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 1000 }, specialNode() }, newNode);
    oldNode = new Object[]{ new long[]{ 1000 }, specialNode() };
    newNode = nonLeafReplaceFirst(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ new long[]{ 1000 }, specialNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode() }, newNode);
    oldNode = new Object[]{ null, standardNode(), standardNode() };
    newNode = nonLeafReplaceFirst(oldNode, specialNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 1000, 5096 }, specialNode(), standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafReplaceFirst(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ new long[]{ 1000, 5096 }, specialNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, newNode);
  }

  @Test
  public void testNonLeafReplaceLast() {
    Object[] oldNode = new Object[]{ null, standardNode() };
    Object[] newNode = nonLeafReplaceLast(oldNode, specialNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 1000 }, specialNode() }, newNode);
    oldNode = new Object[]{ new long[]{ 1000 }, specialNode() };
    newNode = nonLeafReplaceLast(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ new long[]{ 1000 }, specialNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode() }, newNode);
    oldNode = new Object[]{ null, standardNode(), standardNode() };
    newNode = nonLeafReplaceLast(oldNode, specialNode(), 6);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 4096, 5096 }, standardNode(), specialNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafReplaceLast(oldNode, standardNode(), 6);
    assertArrayEquals(new Object[]{ new long[]{ 4096, 5096 }, standardNode(), specialNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode(), standardNode() }, newNode);
  }

  @Test
  public void testNonLeafRemoveFirst() {
    Object[] oldNode = new Object[]{ new long[]{ 4096, 5096, 9192 }, standardNode(), specialNode(), standardNode() };
    Object[] newNode = nonLeafRemoveFirst(oldNode, 6);
    assertArrayEquals(new Object[]{ new long[]{ 4096, 5096, 9192 }, standardNode(), specialNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 1000, 5096 }, specialNode(), standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafRemoveFirst(oldNode, 6);
    assertArrayEquals(new Object[]{ new long[]{ 1000, 5096 }, specialNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafRemoveFirst(oldNode, 6);
    assertArrayEquals(new Object[]{ null, standardNode() }, oldNode);
    assertArrayEquals(new Object[1], newNode);
  }

  @Test
  public void testNonLeafRemoveLast() {
    Object[] oldNode = new Object[]{ new long[]{ 4096, 5096, 9192 }, standardNode(), specialNode(), standardNode() };
    Object[] newNode = nonLeafRemoveLast(oldNode, 6);
    assertArrayEquals(new Object[]{ new long[]{ 4096, 5096, 9192 }, standardNode(), specialNode(), standardNode() }, oldNode);
    assertArrayEquals(new Object[]{ new long[]{ 4096, 5096 }, standardNode(), specialNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafRemoveLast(oldNode, 6);
    assertArrayEquals(new Object[]{ new long[]{ 4096, 5096 }, standardNode(), specialNode() }, oldNode);
    assertArrayEquals(new Object[]{ null, standardNode() }, newNode);
    oldNode = newNode;
    newNode = nonLeafRemoveLast(oldNode, 6);
    assertArrayEquals(new Object[]{ null, standardNode() }, oldNode);
    assertArrayEquals(new Object[1], newNode);
  }

  @Test
  public void testObjectify() {
    assertArrayEquals(new Object[]{ null, (byte) 1, (byte) 2, (byte) 3 }, objectify(new byte[]{ encode(3, false), 1, 2, 3 }));
    assertArrayEquals(new Object[]{ null, 1, 2, 3 }, objectify(new byte[]{ encode(3, true), 1, 2, 3 }));
    assertArrayEquals(new Object[]{ null, "1", "2", "3" }, objectify(new Object[]{ null, "1", "2", "3" }));
  }

  @Test
  public void testTreeFastLookup() {
    final Object[] data = new Object[65];
    int c = 0;
    for (int i = 0; i < data.length - 1; i++) {
      Object[] depth1 = new Object[65];
      for (int j = 0; j < depth1.length - 1; j++) {
        Object[] depth0 = new Object[65];
        for (int k = 0; k < depth1.length - 1; k++) {
          depth0[k + 1] = c++;
        }
        depth1[j + 1] = depth0;
      }
      data[i + 1] = depth1;
    }
    for (int i = 0; i < 64 * 64 * 64; i++) {
      assertEquals(i, Seq.treeFastLookup(data, i, 12));
    }
  }

  @Test
  public void testInsertFirst() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      seq = seq.insertFirst(i);
    }
    for (long i = 0; i < N; i++) {
      assertEquals(N - 1 - i, seq.lookup(i, null));
    }
  }

  @Test
  public void testInsertLast() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      seq = seq.insertLast(i);
    }
    for (long i = 0; i < N; i++) {
      assertEquals(i, seq.lookup(i, null));
    }
  }

  @Test
  public void testRemoveFirst() {
    Seq seq = Seq.EMPTY;
    for (long i = 0; i < N; i++) {
      seq = seq.insertLast(i);
    }
    for (long i = 0; i < N; i++) {
      assertEquals(i, seq.lookup(0, null));
      seq = seq.removeFirst(null);
    }
  }

  @Test
  public void testRemoveLast() {
    Seq seq = EMPTY;
    for (long i = 0; i < N; i++) {
      seq = seq.insertFirst(i);
    }
    for (long i = 0; i < N; i++) {
      assertEquals(i, seq.lookup(seq.length() - 1, null));
      seq = seq.removeLast(null);
    }
  }

  @Test
  public void testFromCharSequence() {
    StringBuilder sb = new StringBuilder();
    for (int codePoint : CODE_POINTS) {
      sb.appendCodePoint(codePoint);
    }
    Seq seq = Seq.fromCharSequence(sb);
    assertOfIntEquals(sb.codePoints().iterator(), new PrimitiveIterator.OfInt() {
      long offset = 0;

      @Override
      public int nextInt() {
        return (int) seq.lookup(offset++, null);
      }

      @Override
      public boolean hasNext() {
        return offset < seq.length();
      }
    });
  }

  @Test
  public void testAsChars() {
    Seq seq = EMPTY;
    //noinspection MismatchedQueryAndUpdateOfStringBuilder
    StringBuilder sb = new StringBuilder();
    for (int codePoint : CODE_POINTS) {
      seq = seq.insertLast(codePoint);
      sb.appendCodePoint(codePoint);
    }
    CharBuffer buffer = CharBuffer.allocate(CODE_POINTS.length * 2);
    assertTrue(seq.asChars(buffer));
    buffer.limit(buffer.position());
    buffer.position(0);
    assertOfIntEquals(sb.codePoints().iterator(), buffer.codePoints().iterator());
  }

  static void assertOfIntEquals(PrimitiveIterator.OfInt expected, PrimitiveIterator.OfInt actual) {
    while (expected.hasNext() && actual.hasNext()) {
      assertEquals(expected.nextInt(), actual.nextInt());
    }
    assertFalse(expected.hasNext());
    assertFalse(actual.hasNext());
  }

  @Test
  public void testAsBytes() {
    Seq seq = EMPTY;
    for (byte b : BYTES) {
      seq = seq.insertLast(b);
    }
    ByteBuffer buffer = ByteBuffer.allocate(BYTES.length);
    assertTrue(seq.asBytes(buffer));
    buffer.position(0);
    for (int i = 0; i < BYTES.length; i++) {
      assertEquals(BYTES[i], buffer.get(i));
    }
  }

  @Test
  public void testSplitCatenate() {
    Seq seq = EMPTY;
    for (long i = 0; i < M; i++) {
      seq = seq.insertLast(i);
    }
    for (long j = 0; j < seq.length(); j++) {
      Seq[] seqs = seq.split(j, null);
      Seq newSeq = catenate(seqs[0], seqs[1]);
      assertEquals(seq, newSeq);
      seq = newSeq;
    }
  }
}
