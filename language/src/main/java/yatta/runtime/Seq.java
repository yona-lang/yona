package yatta.runtime;

import yatta.runtime.exceptions.BadArgException;

import static java.lang.System.arraycopy;

public final class Seq {
  private static final String IOOB_MSG = "Index out of bounds: %d";

  public static final Seq EMPTY = new Seq();

  private final int prefixSize;
  private final long rootSize;
  private final int suffixSize;

  private final Object[] prefix;
  private final Object[] root;
  private final Object[] suffix;

  private long hash = -1L;

  private Seq() {
    prefixSize = 0;
    rootSize = 0;
    suffixSize = 0;
    prefix = newNode(newMeta(0));
    root = newNode(newMeta(1));
    suffix = newNode(newMeta(0));
  }

  private Seq(final Object[] prefix, final int prefixSize,
              final Object[] root, final long rootSize,
              final Object[] suffix, final int suffixSize) {
    this.prefixSize = prefixSize;
    this.rootSize = rootSize;
    this.suffixSize = suffixSize;
    this.prefix = prefix;
    this.root = root;
    this.suffix = suffix;
  }

  public Seq insertLast(final Object o) {
    if (Node.readLength(suffix) != Node.MAX_LEN) return new Seq(prefix, prefixSize, root, rootSize, nodeAppend(suffix, o), suffixSize + 1);
    final Object[] newSuffix = newNode(newMeta(0), o);
    Object[] newRoot = treeAppend(root, suffix);
    if (newRoot == null) newRoot = newLevelAppend(root, suffix);
    return new Seq(prefix, prefixSize, newRoot, rootSize + suffixSize, newSuffix, 1);
  }

  private static Object[] treeAppend(final Object[] tree, final Object[] leaf) {
    final int depth = Meta.readDepth(Node.readMeta(tree));
    if (depth == 1) return Node.readLength(tree) == Node.MAX_LEN ? null : treeAppendTerminal(tree, leaf);
    final Object[] rightmostChild = (Object[]) Node.readAt(tree, Node.readLength(tree) - 1);
    Object[] updatedChild = treeAppend(rightmostChild, leaf);
    if (updatedChild == null) return Node.readLength(tree) == Node.MAX_LEN ? null : treeAppendTerminal(tree, createPath(leaf, depth - 1));
    return treeReplaceLast(tree, updatedChild);
  }

  private static Object[] treeAppendTerminal(final Object[] tree, final Object[] leaf) {
    final Object[] result = nodeAppend(tree, leaf);
    if (!Node.isNormal(leaf)) {
      final byte[] meta = metaAppend(Node.readMeta(tree), Node.calculateSize(leaf));
      Meta.writeBitmap(meta, Util.setBit(Meta.readBitmap(meta), Node.readLength(tree)));
      Node.writeMeta(result, meta);
    }
    return result;
  }

  private static Object[] createPath(Object[] leaf, int depth) {
    while (depth != 0) {
      final byte[] meta;
      if (Node.isNormal(leaf)) {
        meta = newMeta(Meta.readDepth(Node.readMeta(leaf)) + 1);
      } else {
        meta = newMeta(Meta.readDepth(Node.readMeta(leaf)) + 1, 1);
        Meta.writeBitmap(meta, Util.setBit(Meta.EMPTY_BITMAP, 0));
        Meta.writeAt(meta, 0, Node.calculateSize(leaf));
      }
      leaf = newNode(meta, leaf);
      depth--;
    }
    return leaf;
  }

  private static Object[] treeReplaceLast(final Object[] tree, final Object[] replacement) {
    final Object[] result = tree.clone();
    final Object[] old = (Object[]) Node.readAt(tree, Node.readLength(tree) - 1);
    if (Node.isNormal(old)) {
      if (!Node.isNormal(replacement)) {
        final byte[] meta = metaAppend(Node.readMeta(tree), Node.calculateSize(replacement));
        Meta.writeBitmap(meta, Util.setBit(Meta.readBitmap(meta), Node.readLength(tree) - 1));
        Node.writeMeta(result, meta);
      }
    } else {
      if (Node.isNormal(replacement)) {
        final byte[] meta = metaRemoveLast(Node.readMeta(tree));
        Meta.writeBitmap(meta, Util.clearBit(Meta.readBitmap(meta), Node.readLength(tree) - 1));
        Node.writeMeta(result, meta);
      } else {
        final byte[] meta = Node.readMeta(tree).clone();
        Meta.writeAt(meta, Meta.readLength(meta) - 1, Node.calculateSize(replacement));
        Node.writeMeta(result, meta);
      }
    }
    Node.writeAt(result, Node.readLength(result) - 1, replacement);
    return result;
  }

  private static Object[] newLevelAppend(final Object[] root, Object[] leaf) {
    final int rootDepth = Meta.readDepth(Node.readMeta(root));
    leaf = createPath(leaf, rootDepth);
    final byte[] meta;
    if (Node.isNormal(root)) {
      if (Node.isNormal(leaf)) {
        meta = newMeta(rootDepth + 1);
      } else {
        meta = newMeta(rootDepth + 1, 1);
        final short bitmap = Util.setBit(Meta.EMPTY_BITMAP, 1);
        Meta.writeBitmap(meta, bitmap);
        Meta.writeAt(meta, 0, Node.calculateSize(leaf));
      }
    } else {
      if (Node.isNormal(leaf)) {
        meta = newMeta(rootDepth + 1, 1);
        final short bitmap = Util.setBit(Meta.EMPTY_BITMAP, 0);
        Meta.writeBitmap(meta, bitmap);
        Meta.writeAt(meta, 0, Node.calculateSize(root));
      } else {
        meta = newMeta(rootDepth + 1, 2);
        short bitmap = Meta.EMPTY_BITMAP;
        bitmap = Util.setBit(bitmap, 0);
        bitmap = Util.setBit(bitmap, 1);
        Meta.writeBitmap(meta, bitmap);
        Meta.writeAt(meta, 0, Node.calculateSize(root));
        Meta.writeAt(meta, 1, Node.calculateSize(leaf));
      }
    }
    return newNode(meta, root, leaf);
  }

  public Seq insertLastEncoded(final byte[] b, final int size, final EncodedType type) {
    final long sizeAndType = encodeSizeAndType(size, type);
    if (Node.readLength(suffix) != Node.MAX_LEN) {
      final byte[] oldMeta = Node.readMeta(suffix);
      final byte[] newMeta = metaAppend(Node.readMeta(suffix), sizeAndType);
      final short oldBitmap = Meta.readBitmap(oldMeta);
      final short newBitmap = Util.setBit(oldBitmap, Node.readLength(suffix));
      Meta.writeBitmap(newMeta, newBitmap);
      final Object[] newSuffix = nodeAppend(suffix, b);
      Node.writeMeta(newSuffix, newMeta);
      return new Seq(prefix, prefixSize, root, rootSize, newSuffix, suffixSize + size);
    }
    final byte[] newMeta = newMeta(0, 1);
    Meta.writeBitmap(newMeta, Util.setBit(Meta.EMPTY_BITMAP, 0));
    Meta.writeAt(newMeta, 0, sizeAndType);
    final Object[] newSuffix = newNode(newMeta, b);
    Object[] newRoot = treeAppend(root, suffix);
    if (newRoot == null) newRoot = newLevelAppend(root, suffix);
    return new Seq(prefix, prefixSize, newRoot, rootSize + suffixSize, newSuffix, size);
  }

  public Object lookup(final long idx, final com.oracle.truffle.api.nodes.Node node) {
    if (idx < 0) throw new BadArgException(String.format(IOOB_MSG, idx), node);
    long i = idx;
    if (i < prefixSize) return lookupLeaf(prefix, i);
    i -= prefixSize;
    if (i < rootSize) return lookupTree(root, i);
    i -= rootSize;
    if (i < suffixSize) return lookupLeaf(suffix, i);
    throw new BadArgException(String.format(IOOB_MSG, idx), node);
  }

  private static Object lookupLeaf(final Object[] leaf, long idx) {
    final byte[] meta = Node.readMeta(leaf);
    final short bitmap = Meta.readBitmap(meta);
    if (bitmap == 0) return Node.readAt(leaf, (int) idx);
    int j = 0;
    for (int i = 0; i < Node.readLength(leaf); i++) {
      if (Util.testBit(bitmap, i)) {
        final long sizeAndType = Meta.readAt(meta, j++);
        final int size = (int) decodeSize(sizeAndType);
        if (idx < size) {
          final byte[] bytes = (byte[]) Node.readAt(leaf, i);
          if (decodeType(sizeAndType) == EncodedType.UTF8) {
            return Util.codePointAt(bytes, offsetUtf8(bytes, (int) idx, size));
          } else {
            return bytes[(int) idx];
          }
        } else {
          idx -= size;
        }
      } else {
        if (idx < 1) {
          return Node.readAt(leaf, i);
        } else {
          idx--;
        }
      }
    }
    throw new AssertionError();
  }

  static int offsetUtf8(final byte[] bytes, int idx, final int len) {
    if (idx < len / 2) {
      int offset = 0;
      while (idx > 0) {
        switch ((0xf0 & bytes[offset]) >>> 4) {
          case 0b0000:
          case 0b0001:
          case 0b0010:
          case 0b0011:
          case 0b0100:
          case 0b0101:
          case 0b0110:
          case 0b0111:
            offset += 1;
            break;
          case 0b1000:
          case 0b1001:
          case 0b1010:
          case 0b1011:
            throw new AssertionError();
          case 0b1100:
          case 0b1101:
            offset += 2;
            break;
          case 0b1110:
            offset += 3;
            break;
          case 0b1111:
            offset += 4;
        }
        idx--;
      }
      return offset;
    } else {
      idx = len - idx - 1;
      int offset = bytes.length;
      while (idx >= 0) {
        if (((0xf0 & bytes[--offset]) >>> 6) != 0b10) idx--;
      }
      return offset;
    }
  }

  private static Object lookupTree(Object[] node, long idx) {
    final byte[] meta = Node.readMeta(node);
    int depth = Meta.readDepth(meta);
    if (depth == 0) return lookupLeaf(node, idx);
    final short bitmap = Meta.readBitmap(meta);
    if (bitmap == 0) {
      do {
        final long elementSize = defaultElementSize(depth);
        node = (Object[]) Node.readAt(node, (int) (idx / elementSize));
        idx = idx % elementSize;
        depth--;
      } while (depth != 0);
      return Node.readAt(node, (int) idx);
    } else {
      int j = 0;
      for (int i = 0; i < Node.readLength(node); i++) {
        final long size = Util.testBit(bitmap, i) ? Meta.readAt(meta, j++) : defaultElementSize(depth);
        if (idx < size) return lookupTree((Object[]) Node.readAt(node, i), idx);
        idx -= size;
      }
    }
    throw new AssertionError();
  }

  private static long defaultElementSize(final int depth) {
    return 1L << (depth << 2);
  }

  private static Object[] newNode(final byte[] meta) {
    return new Object[]{ meta };
  }

  private static Object[] newNode(final byte[] meta, final Object sole) {
    return new Object[]{ meta, sole };
  }

  private static Object[] newNode(final byte[] meta, final Object first, final Object second) {
    return new Object[]{ meta, first, second };
  }

  private static Object[] nodeAppend(final Object[] node, final Object o) {
    final Object[] result = new Object[node.length + 1];
    arraycopy(node, 0, result, 0, node.length);
    result[node.length] = o;
    return result;
  }

  private static final class Node {
    static final int MAX_LEN = 16;

    static int readLength(final Object[] node) {
      return node.length - 1;
    }

    static byte[] readMeta(final Object[] node) {
      return (byte[]) node[0];
    }

    static void writeMeta(final Object[] node, final byte[] meta) {
      node[0] = meta;
    }

    static Object readAt(final Object[] node, final int idx) {
      return node[idx + 1];
    }

    static void writeAt(final Object[] node, final int idx, final Object o) {
      node[idx + 1] = o;
    }

    static boolean isNormal(final Object[] node) {
      return readLength(node) == MAX_LEN && Meta.readBitmap(readMeta(node)) == 0;
    }

    static long calculateSize(final Object[] node) {
      final byte[] meta = readMeta(node);
      final int depth = Meta.readDepth(meta);
      final long defaultSize = defaultElementSize(depth);
      final short bitmap = Meta.readBitmap(meta);
      long result = 0;
      int extraIdx = 0;
      for (int i = 0; i < readLength(node); i++) {
        if (Util.testBit(bitmap, i)) {
          result += decodeSize(Meta.readAt(meta, extraIdx++));
        } else {
          result += defaultSize;
        }
      }
      return result;
    }
  }

  private static byte[] newMeta(final int depth) {
    return new byte[]{(byte) depth, 0, 0 };
  }

  private static byte[] newMeta(final int depth, final int len) {
    final byte[] result = new byte[3 + len * 8];
    result[0] = (byte) depth;
    return result;
  }

  private static byte[] metaAppend(final byte[] meta, final long value) {
    final byte[] result = new byte[meta.length + 8];
    arraycopy(meta, 0, result, 0, meta.length);
    Util.int64Write(value, result, meta.length);
    return result;
  }

  private static byte[] metaRemoveLast(final byte[] meta) {
    final byte[] result = new byte[meta.length - 8];
    arraycopy(meta, 0, result, 0, result.length);
    return result;
  }

  private static final class Meta {
    static final short EMPTY_BITMAP = 0;

    static int readLength(final byte[] meta) {
      return (meta.length - 3) / 8;
    }

    static int readDepth(final byte[] meta) {
      return meta[0];
    }

    static short readBitmap(final byte[] meta) {
      return Util.int16Read(meta, 1);
    }

    static void writeBitmap(final byte[] meta, final short bitmap) {
      Util.int16Write(bitmap, meta, 1);
    }

    static long readAt(final byte[] meta, final int idx) {
      return Util.int64Read(meta, 3 + 8 * idx);
    }

    static void writeAt(final byte[] meta, final int idx, final long value) {
      Util.int64Write(value, meta, 3 + 8 * idx);
    }
  }

  private static long encodeSizeAndType(final long size, final EncodedType type) {
    return size | ((type == EncodedType.UTF8) ? 0x8000000000000000L : 0x0L);
  }

  private static long decodeSize(final long sizeAndType) {
    return sizeAndType & 0x7fffffffffffffffL;
  }

  private static EncodedType decodeType(final long sizeAndType) {
    return (sizeAndType & 0x8000000000000000L) == 0 ? EncodedType.BYTES : EncodedType.UTF8;
  }

  public enum EncodedType {
    BYTES, UTF8
  }
}
