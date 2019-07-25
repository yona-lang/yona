package yatta.runtime;

import yatta.runtime.exceptions.BadArgException;

import static java.lang.System.arraycopy;

public final class Seq {
  private static final int MAX_NODE_LENGTH = 16;

  private static final byte[] EMPTY_LEAF_META = Meta.newMeta(0);
  private static final byte[] EMPTY_ROOT_META = Meta.newMeta(1);

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
    prefix = Node.newEmptyNode(0);
    root = Node.newEmptyNode(1);
    suffix = Node.newEmptyNode(0);
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

  public Object lookup(final long index, final com.oracle.truffle.api.nodes.Node node) {
    long idx = index;
    if (idx < 0) throw new BadArgException("Index out of bounds: " + index, node);
    if (idx < prefixSize) {
      return leafLookup(prefix, (int) idx);
    }
    idx -= prefixSize;
    if (idx < rootSize) {

    }
    idx -= rootSize;
    if (idx < suffixSize) {
      return leafLookup(suffix, (int) idx);
    }
    throw new BadArgException("Index out of bounds: " + index, node);
  }

  private static Object leafLookup(final Object[] leaf, final int idx) {
    return leaf[idx]; // TODO: adapt for bytestr
  }

  private static Object treeLookup(final Object[] tree, final int idx) {
    return null; // TODO
  }

  public Seq inject(final Object o) {
    if (Node.getLength(suffix) == MAX_NODE_LENGTH) {
      // suffix overflow
      final Object[] newSuffix = Node.newSingletonLeaf(o);
      Object[] newRoot = treeAppend(root, suffix);
      if (newRoot == null) {
        // root overflow
        final int oldDepth = Meta.getDepth(Node.getMeta(root));
        newRoot = createParent(root, createPath(suffix, oldDepth), oldDepth + 1);
      }
      return new Seq(prefix, prefixSize, newRoot, rootSize + suffixSize, newSuffix, 1);
    } else {
      return new Seq(prefix, prefixSize, root, rootSize, Node.leafAppend(suffix, o), suffixSize + 1);
    }
  }

  private static Object[] treeAppend(final Object[] tree, final Object[] leaf) {
    final int treeDepth = Meta.getDepth(Node.getMeta(tree));
    if (treeDepth == 1) {
      // just append now
      return Node.getLength(tree) == MAX_NODE_LENGTH ? null : Node.append(tree, leaf);
    }
    // fetch rightmost child and go deeper
    final Object[] rightmostChild = (Object[]) Node.lookup(tree, Node.getLength(tree) - 1);
    Object[] updatedChild = treeAppend(rightmostChild, leaf);
    if (updatedChild == null) {
      // overflow in rightmost child
      return Node.getLength(tree) == MAX_NODE_LENGTH ? null : Node.append(tree, createPath(leaf, treeDepth - 1));
    } else {
      return replaceRightmostChild(tree, updatedChild);
    }
  }

  private static Object[] replaceRightmostChild(final Object[] parent, final Object[] replacement) {
    final Object[] result = parent.clone();
    result[result.length - 1] = replacement;
    final byte[] parentMeta = Node.getMeta(parent);
    final short parentBitmap = Meta.getBitmap(parentMeta);
    final int idx = parent.length - 2;
    final byte[] resultMeta;
    if (Node.isNormal(replacement)) {
      // replacement node is normal, make sure it is not marked in parent's meta
      final short resultBitmap = Util.clearBit(parentBitmap, idx);
      if (resultBitmap != parentBitmap) {
        // old meta had this bit marked, we need to remove last varInt from it
        resultMeta = metaCopyEjectVarInt(parentMeta);
        Util.int16Write(resultBitmap, resultMeta, 1);
      } else {
        resultMeta = parentMeta;
      }
    } else {
      // replacement node is special, make sure it's marked in parent's meta
      final short resultBitmap = Util.setBit(parentBitmap, idx);
      final long replacementLength = Node.calculateSize(replacement, elementSize(Meta.getDepth(Node.getMeta(replacement))));
      if (resultBitmap != parentBitmap) {
        // old bitmap didn't have this bit marked, so we add
        resultMeta = metaCopyInjectVarInt(parentMeta, replacementLength);
        Util.int16Write(resultBitmap, resultMeta, 1);
      } else {
        resultMeta = metaCopyReplaceLastVarInt(parentMeta, replacementLength);
      }
    }
    result[0] = resultMeta;
    return result;
  }

  private static byte[] metaCopyInjectVarInt(final byte[] meta, final long value) {
    final byte[] result = new byte[meta.length + Util.varInt63Len(value)];
    arraycopy(meta, 0, result, 0, meta.length);
    Util.varInt63Write(value, result, meta.length);
    return result;
  }

  private static byte[] metaCopyEjectVarInt(final byte[] meta) {
    int len = lastVarIntIdx(meta);
    final byte[] result = new byte[len];
    arraycopy(meta, 0, result, 0, len);
    return result;
  }

  private static byte[] metaCopyReplaceLastVarInt(final byte[] meta, final long value) {
    int len = lastVarIntIdx(meta);
    final byte[] result = new byte[len + Util.varInt63Len(value)];
    arraycopy(meta, 0, result, 0, len);
    Util.varInt63Write(value, result, len);
    return result;
  }

  private static int lastVarIntIdx(final byte[] meta) {
    int result = 3;
    for (int offset = result; offset < meta.length;) {
      result = offset;
      offset += Util.varInt63Len(Util.varInt63Read(meta, offset));
    }
    return result;
  }

  private static Object[] createParent(final Object[] left, final Object[] right, final int depth) {
    return Node.newBranch(Meta.newMeta(depth, left, right), left, right);
  }

  private static Object[] createPath(Object[] leaf, int depth) {
    while (depth != 0) {
      final byte[] meta = Meta.newMeta(Meta.getDepth(Node.getMeta(leaf)) + 1, leaf);
      leaf = Node.newBranch(meta, leaf);
      depth--;
    }
    return leaf;
  }

  private static int elementSize(final int depth) {
    return 1 << (depth << 2);
  }

  private static final class Node {

    static Object[] newEmptyNode(final int depth) {
      return new Object[]{ Meta.newMeta(depth) };
    }

    static Object[] newSingletonLeaf(final Object o) {
      return new Object[]{ Meta.newMeta(0), o };
    }

    static Object[] newBranch(final byte[] meta, final Object[] node) {
      return new Object[]{ meta, node };
    }

    static Object[] newBranch(final byte[] meta, final Object[] first, final Object[] second) {
      return new Object[]{ meta, first, second };
    }

    static boolean isNormal(final Object[] node) {
      return getLength(node) == MAX_NODE_LENGTH && Meta.getBitmap(getMeta(node)) == Meta.EMPTY_BITMAP;
    }

    static byte[] getMeta(final Object[] node) {
      return (byte[]) node[0];
    }

    static int getLength(final Object[] node) {
      return node.length - 1;
    }

    static Object lookup(final Object[] node, final int idx) {
      return node[idx + 1];
    }

    static Object[] leafAppend(final Object[] node, final Object o) {
      final Object[] result = new Object[node.length + 1];
      arraycopy(node, 0, result, 0, node.length);
      result[node.length] = o;
      return result;
    }

    static long calculateSize(final Object[] node, final long defaultElementSize) {
      long result = 0;
      final byte[] meta = getMeta(node);
      final short bitmap = Meta.getBitmap(meta);
      int offset = Meta.EXTRA_OFFSET;
      for (int i = 0; i < getLength(node); i++) {
        if (Util.testBit(bitmap, i)) {
          final long increment = Util.varInt63Read(meta, offset);
          offset += Util.varInt63Len(increment);
          result += increment;
        } else {
          result += defaultElementSize;
        }
      }
      return result;
    }

    static Object[] append(final Object[] parent, final Object[] child) {
      final Object[] result = new Object[parent.length + 1];
      arraycopy(parent, 0, result, 0, parent.length);
      result[parent.length] = child;
      final byte[] childMeta = Node.getMeta(child);
      if (!isNormal(child)) {
        final long childSize = calculateSize(child, elementSize(Meta.getDepth(childMeta)));
        final byte[] oldMeta = Node.getMeta(parent);
        final byte[] resultMeta = new byte[oldMeta.length + Util.varInt63Len(childSize)];
        resultMeta[0] = oldMeta[0];
        Util.int16Write(Util.setBit(Meta.getBitmap(oldMeta), Node.getLength(result) - 1), resultMeta, 1);
        Util.varInt63Write(childSize, resultMeta, oldMeta.length);
      }
      return result;
    }
  }

  private static final class Meta {
    static final int EXTRA_OFFSET = 3;
    static final short EMPTY_BITMAP = 0;

    static byte[] newMeta(final int depth) {
      return new byte[]{ (byte) depth, 0, 0 };
    }

    static byte[] newMeta(final int depth, final Object[] child) {
      if (Node.isNormal(child)) {
        return new byte[]{(byte) depth, 0, 0 };
      } else {
        final long childSize = Node.calculateSize(child, elementSize(depth - 1));
        final byte[] result = new byte[3 + Util.varInt63Len(childSize)];
        result[0] = (byte) depth;
        Util.int16Write(Util.setBit(EMPTY_BITMAP, 0), result, 1);
        Util.varInt63Write(childSize, result, 3);
        return result;
      }
    }

    static byte[] newMeta(final int depth, final Object[] firstChild, final Object[] secondChild) {
      final boolean firstNormal = Node.isNormal(firstChild);
      final boolean secondNormal = Node.isNormal(secondChild);
      final int elementSize = elementSize(depth - 1);
      short bitmap = EMPTY_BITMAP;
      final byte[] result;
      if (!firstNormal) {
        bitmap = Util.setBit(bitmap, 0);
        final long firstSize = Node.calculateSize(firstChild, elementSize);
        if (!secondNormal) {
          bitmap = Util.setBit(bitmap, 1);
          final long secondSize = Node.calculateSize(secondChild, elementSize);
          final int firstSizeLen = Util.varInt63Len(firstSize);
          final int secondSizeLen = Util.varInt63Len(secondSize);
          result = new byte[3 + firstSizeLen + secondSizeLen];
          result[0] = (byte) depth;
          Util.int16Write(bitmap, result, 1);
          Util.varInt63Write(firstSize, result, 3);
          Util.varInt63Write(secondSize, result, 3 + firstSizeLen);
        } else {
          result = new byte[3 + Util.varInt63Len(firstSize)];
          result[0] = (byte) depth;
          Util.int16Write(bitmap, result, 1);
          Util.varInt63Write(firstSize, result, 3);
        }
      } else {
        if (!secondNormal) {
          bitmap = Util.setBit(bitmap, 1);
          final long secondSize = Node.calculateSize(secondChild, elementSize);
          result = new byte[3 + Util.varInt63Len(secondSize)];
          result[0] = (byte) depth;
          Util.int16Write(bitmap, result, 1);
          Util.varInt63Write(secondSize, result, 3);
        } else {
          result = new byte[3];
          result[0] = (byte) depth;
          Util.int16Write(bitmap, result, 1);
        }
      }
      return result;
    }

    static int getDepth(final byte[] meta) {
      return meta[0];
    }

    static short getBitmap(final byte[] meta) {
      return Util.int16Read(meta, 1);
    }
  }

  public static void main(String[] args) {
    Seq seq = new Seq();
    for (int i = 0; i < Integer.MAX_VALUE; i++) {
      seq = seq.inject(i);
    }
  }
}
