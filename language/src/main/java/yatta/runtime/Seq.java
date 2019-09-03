package yatta.runtime;

import yatta.runtime.exceptions.BadArgException;

public final class Seq {
  private static final byte[] DEFAULT_LEAF_META = metaNew((byte) 0);
  private static final Object[] EMPTY_LEAF = nodeNew(DEFAULT_LEAF_META);
  private static final byte[] DEFAULT_ROOT_META = metaNew((byte) 1);
  private static final Object[] EMPTY_ROOT = nodeNew(DEFAULT_ROOT_META);
  private static final String IOOB_MSG = "Index out of bounds: %d";

  public static final Seq EMPTY = new Seq(EMPTY_LEAF, 0, EMPTY_ROOT, 0, EMPTY_LEAF, 0);

  private final int prefixSize;
  private final long rootSize;
  private final int suffixSize;

  private final Object[] prefix;
  private final Object[] root;
  private final Object[] suffix;

  private long hash = -1L;

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

  public Seq insertFirst(final Object o) {
    if (nodeLength(prefix) != 16) return new Seq(leafInsertFirst(prefix, o), prefixSize + 1, root, rootSize, suffix, suffixSize);
    final Object[] newPrefix = nodeNew(DEFAULT_LEAF_META, o);
    Object[] newRoot = tryTreeInsertFirst(root, prefix);
    if (newRoot == null) newRoot = newLevel(wrap(prefix, metaDepth(nodeMeta(root))), root);
    return new Seq(newPrefix, 1, newRoot, rootSize + prefixSize, suffix, suffixSize);
  }

  private static Object[] leafInsertFirst(final Object[] leaf, final Object o) {
    final byte[] leafMeta = nodeMeta(leaf);
    final byte[] resultMeta;
    if (!metaIsEmpty(leafMeta)) {
      resultMeta = metaCopy(leafMeta);
      metaShiftRight(resultMeta);
    } else resultMeta = leafMeta;
    return nodeCopyInsertFirst(leaf, resultMeta, o);
  }

  private static Object[] tryTreeInsertFirst(final Object[] tree, final Object[] leaf) {
    final byte[] treeMeta = nodeMeta(tree);
    final byte treeDepth = metaDepth(treeMeta);
    if (treeDepth == 1) return tryTreeInsertFirstTerminal(tree, leaf);
    Object[] firstChild = (Object[]) nodeDataAt(tree, 0);
    firstChild = tryTreeInsertFirst(firstChild, leaf);
    if (firstChild == null) return tryTreeInsertFirstTerminal(tree, wrap(leaf, treeDepth - 1));
    return treeReplaceFirst(tree, firstChild);
  }

  private static Object[] wrap(Object[] node, final int depth) {
    byte nodeDepth = metaDepth(nodeMeta(node));
    while (nodeDepth != depth) {
      nodeDepth++;
      final byte[] newMeta;
      if (nodeIsSpecial(node)) {
        newMeta = metaNew(nodeDepth, calculateTotalSize(node));
        metaSetBit(newMeta, 0);
      } else newMeta = metaNew(nodeDepth);
      node = nodeNew(newMeta, node);
    }
    return node;
  }

  private static Object[] treeReplaceFirst(final Object[] tree, final Object[] leaf) {
    final byte[] treeMeta = nodeMeta(tree);
    final byte[] resultMeta;
    if (nodeIsSpecial(leaf)) {
      final long leafSize = calculateTotalSize(leaf);
      if (metaTestBit(treeMeta, 0)) resultMeta = metaCopyReplaceFirst(treeMeta, leafSize);
      else {
        resultMeta = metaCopyInsertFirst(treeMeta, leafSize);
        metaSetBit(resultMeta, 0);
      }
    } else {
      if (metaTestBit(treeMeta, 0)) {
        resultMeta = metaCopyRemoveFirst(treeMeta);
        metaClearBit(resultMeta, 0);
      } else resultMeta = treeMeta;
    }
    return nodeCopyReplaceFirst(tree, resultMeta, leaf);
  }

  private static long calculateTotalSize(final Object[] node) {
    final byte[] meta = nodeMeta(node);
    final byte depth = metaDepth(meta);
    final long defaultSize = elementSizeAtDepth(depth);
    long result = 0;
    int extraIdx = 0;
    for (int i = 0; i < nodeLength(node); i++) {
      if (metaTestBit(meta, i)) result += decodeSize(metaDataAt(meta, extraIdx++));
      else result += defaultSize;
    }
    return result;
  }

  private static Object[] tryTreeInsertFirstTerminal(final Object[] tree, final Object[] leaf) {
    if (nodeLength(tree) == 16) return null;
    final byte[] treeMeta = nodeMeta(tree);
    final byte[] newMeta;
    if (nodeIsSpecial(leaf)) {
      newMeta = metaCopyInsertFirst(treeMeta, calculateTotalSize(leaf));
      metaShiftRight(newMeta);
      metaSetBit(newMeta, 0);
    } else if (!metaIsEmpty(treeMeta)) {
      newMeta = metaCopy(treeMeta);
      metaShiftRight(newMeta);
    } else newMeta = treeMeta;
    return nodeCopyInsertFirst(tree, newMeta, leaf);
  }

  private static Object[] newLevel(final Object[] first, final Object[] second) {
    final byte resultDepth = (byte) (metaDepth(nodeMeta(first)) + 1);
    final byte[] resultMeta;
    if (nodeIsSpecial(first)) {
      final long firstSize = calculateTotalSize(first);
      if (nodeIsSpecial(second)) {
        final long secondSize = calculateTotalSize(second);
        resultMeta = metaNew(resultDepth, firstSize, secondSize);
        metaSetBit(resultMeta, 0);
        metaSetBit(resultMeta, 1);
      } else {
        resultMeta = metaNew(resultDepth, firstSize);
        metaSetBit(resultMeta, 0);
      }
    } else {
      if (nodeIsSpecial(second)) {
        final long secondSize = calculateTotalSize(second);
        resultMeta = metaNew(resultDepth, secondSize);
        metaSetBit(resultMeta, 1);
      } else resultMeta = metaNew(resultDepth);
    }
    return nodeNew(resultMeta, first, second);
  }

  public Seq insertFirstEncoded(final byte[] bytes, final int size, final boolean utf8) {
    if (nodeLength(prefix) != 16) return new Seq(leafInsertFirstEncoded(prefix, bytes, size, utf8), prefixSize + size, root, rootSize, suffix, suffixSize);
    final byte[] newPrefixMeta = metaNew((byte) 0, encodeSizeAndType(size, utf8));
    metaSetBit(newPrefixMeta, 0);
    final Object[] newPrefix = nodeNew(newPrefixMeta, bytes);
    Object[] newRoot = tryTreeInsertFirst(root, prefix);
    if (newRoot == null) newRoot = newLevel(wrap(prefix, metaDepth(nodeMeta(root))), root);
    return new Seq(newPrefix, size, newRoot, rootSize + prefixSize, suffix, suffixSize);
  }

  private static Object[] leafInsertFirstEncoded(final Object[] leaf, final byte[] bytes, final int size, final boolean utf8) {
    final byte[] resultMeta = metaCopyInsertFirst(nodeMeta(leaf), encodeSizeAndType(size, utf8));
    metaShiftRight(resultMeta);
    metaSetBit(resultMeta, 0);
    return nodeCopyInsertFirst(leaf, resultMeta, bytes);
  }

  public Seq insertLast(final Object o) {
    if (nodeLength(suffix) != 16) return new Seq(prefix, prefixSize, root, rootSize, leafInsertLast(suffix, o), suffixSize + 1);
    final Object[] newSuffix = nodeNew(DEFAULT_LEAF_META, o);
    Object[] newRoot = tryTreeInsertLast(root, suffix);
    if (newRoot == null) newRoot = newLevel(root, wrap(suffix, metaDepth(nodeMeta(root))));
    return new Seq(prefix, prefixSize, newRoot, rootSize + suffixSize, newSuffix, 1);
  }

  private static Object[] leafInsertLast(final Object[] leaf, final Object o) {
    return nodeCopyInsertLast(leaf, nodeMeta(leaf), o);
  }

  private static Object[] tryTreeInsertLast(final Object[] tree, final Object[] leaf) {
    final byte[] treeMeta = nodeMeta(tree);
    final byte treeDepth = metaDepth(treeMeta);
    if (treeDepth == 1) return tryTreeInsertLastTerminal(tree, leaf);
    Object[] lastChild = (Object[]) nodeDataAt(tree, nodeLength(tree) - 1);
    lastChild = tryTreeInsertLast(lastChild, leaf);
    if (lastChild == null) return tryTreeInsertLastTerminal(tree, wrap(leaf, treeDepth - 1));
    return treeReplaceLast(tree, lastChild);
  }

  private static Object[] tryTreeInsertLastTerminal(final Object[] tree, final Object[] leaf) {
    if (nodeLength(tree) == 16) return null;
    final byte[] treeMeta = nodeMeta(tree);
    final byte[] newMeta;
    if (nodeIsSpecial(leaf)) {
      newMeta = metaCopyInsertLast(treeMeta, calculateTotalSize(leaf));
      metaSetBit(newMeta, nodeLength(tree));
    } else newMeta = treeMeta;
    return nodeCopyInsertLast(tree, newMeta, leaf);
  }

  private static Object[] treeReplaceLast(final Object[] tree, final Object[] leaf) {
    final byte[] treeMeta = nodeMeta(tree);
    final byte[] resultMeta;
    if (nodeIsSpecial(leaf)) {
      final long leafSize = calculateTotalSize(leaf);
      if (metaTestBit(treeMeta, nodeLength(tree) - 1)) resultMeta = metaCopyReplaceLast(treeMeta, leafSize);
      else {
        resultMeta = metaCopyInsertLast(treeMeta, leafSize);
        metaSetBit(resultMeta, nodeLength(tree) - 1);
      }
    } else {
      if (metaTestBit(treeMeta, nodeLength(tree) - 1)) {
        resultMeta = metaCopyRemoveLast(treeMeta);
        metaClearBit(resultMeta, nodeLength(tree) - 1);
      } else resultMeta = treeMeta;
    }
    return nodeCopyReplaceLast(tree, resultMeta, leaf);
  }

  public Seq insertLastEncoded(final byte[] bytes, final int size, final boolean utf8) {
    if (nodeLength(suffix) != 16) return new Seq(prefix, prefixSize, root, rootSize, leafInsertLastEncoded(suffix, bytes, size, utf8), suffixSize + size);
    final byte[] newSuffixMeta = metaNew((byte) 0, encodeSizeAndType(size, utf8));
    metaSetBit(newSuffixMeta, 0);
    final Object[] newSuffix = nodeNew(newSuffixMeta, bytes);
    Object[] newRoot = tryTreeInsertLast(root, suffix);
    if (newRoot == null) newRoot = newLevel(root, wrap(suffix, metaDepth(nodeMeta(root))));
    return new Seq(prefix, prefixSize, newRoot, rootSize + suffixSize, newSuffix, size);
  }

  private static Object[] leafInsertLastEncoded(final Object[] leaf, final byte[] bytes, final int size, final boolean utf8) {
    final byte[] resultMeta = metaCopyInsertLast(nodeMeta(leaf), encodeSizeAndType(size, utf8));
    metaSetBit(resultMeta, nodeLength(leaf));
    return nodeCopyInsertLast(leaf, resultMeta, bytes);
  }

  public Seq removeFirst() {
    if (prefixSize != 0) return new Seq(leafRemoveFirst(prefix), prefixSize - 1, root, rootSize, suffix, suffixSize);
    if (rootSize != 0) return new Seq(EMPTY_LEAF, 0, treeRemoveFirst(root), rootSize - 1, suffix, suffixSize);
    if (suffixSize != 0) return new Seq(EMPTY_LEAF, 0, EMPTY_ROOT, 0, leafRemoveFirst(suffix), suffixSize - 1);
    return null;
  }

  private static Object[] leafRemoveFirst(final Object[] leaf) {
    final byte[] leafMeta = nodeMeta(leaf);
    if (metaTestBit(leafMeta, 0)) {
      final long firstSizeAndType = metaDataAt(leafMeta, 0);
      final int firstSize = (int) decodeSize(firstSizeAndType);
      switch (firstSize) {
        case 1: {
          final byte[] newMeta = metaCopyRemoveFirst(leafMeta);
          metaShiftLeft(newMeta);
          return nodeCopyRemoveFirst(leaf, newMeta);
        }
        case 2: {
          final byte[] newMeta = metaCopyRemoveFirst(leafMeta);
          metaClearBit(newMeta, 0);
          final byte[] oldFirst = (byte[]) nodeDataAt(leaf, 0);
          final Object newFirst;
          if (decodeIsUtf8(firstSizeAndType)) {
            final int offset = offsetUtf8(oldFirst, 1);
            newFirst = Util.codePointAt(oldFirst, offset);
          } else newFirst = oldFirst[1];
          return nodeCopyReplaceFirst(leaf, newMeta, newFirst);
        }
        default: {
          final boolean utf8 = decodeIsUtf8(firstSizeAndType);
          final byte[] newMeta = metaCopyReplaceFirst(leafMeta, encodeSizeAndType(firstSize - 1, utf8));
          final byte[] oldFirst = (byte[]) nodeDataAt(leaf, 0);
          final int offset = utf8 ? offsetUtf8(oldFirst, 1) : 1;
          final int newFirstLength = oldFirst.length - offset;
          final byte[] newFirst = new byte[newFirstLength];
          System.arraycopy(oldFirst, offset, newFirst, 0, newFirstLength);
          return nodeCopyReplaceFirst(leaf, newMeta, newFirst);
        }
      }
    } else {
      final byte[] newMeta;
      if (metaIsEmpty(leafMeta)) newMeta = leafMeta;
      else {
        newMeta = metaCopy(leafMeta);
        metaShiftLeft(newMeta);
      }
      return nodeCopyRemoveFirst(leaf, newMeta);
    }
  }

  private static Object[] treeRemoveFirst(final Object[] tree) {
    final byte[] treeMeta = nodeMeta(tree);
    final byte treeDepth = metaDepth(treeMeta);
    Object[] firstChild = (Object[]) nodeDataAt(tree, 0);
    if (treeDepth == 1) firstChild = leafRemoveFirst(firstChild);
    else firstChild = treeRemoveFirst(firstChild);
    if (nodeLength(firstChild) == 0) return treeRemoveFirstTerminal(tree);
    return treeReplaceFirst(tree, firstChild);
  }

  private static Object[] treeRemoveFirstTerminal(final Object[] tree) {
    final byte[] oldMeta = nodeMeta(tree);
    final byte[] newMeta;
    if (metaTestBit(oldMeta, 0)) newMeta = metaCopyRemoveFirst(oldMeta);
    else newMeta = metaCopy(oldMeta);
    metaShiftLeft(newMeta);
    return nodeCopyRemoveFirst(tree, newMeta);
  }

  public Seq removeLast() {
    if (suffixSize != 0) return new Seq(prefix, prefixSize, root, rootSize, leafRemoveLast(suffix), suffixSize - 1);
    if (rootSize != 0) return new Seq(prefix, prefixSize, treeRemoveLast(root), rootSize - 1, EMPTY_LEAF, 0);
    if (prefixSize != 0) return new Seq(leafRemoveLast(prefix), prefixSize - 1, EMPTY_ROOT, 0, EMPTY_LEAF, 0);
    return null;
  }

  private static Object[] leafRemoveLast(final Object[] leaf) {
    final byte[] leafMeta = nodeMeta(leaf);
    final int idx = nodeLength(leaf) - 1;
    if (metaTestBit(leafMeta, idx)) {
      final long lastSizeAndType = metaDataAt(leafMeta, idx);
      final int lastSize = (int) decodeSize(lastSizeAndType);
      switch (lastSize) {
        case 1: {
          final byte[] newMeta = metaCopyRemoveLast(leafMeta);
          metaClearBit(newMeta, idx);
          return nodeCopyRemoveLast(leaf, newMeta);
        }
        case 2: {
          final byte[] newMeta = metaCopyRemoveLast(leafMeta);
          metaClearBit(newMeta, idx);
          final byte[] oldLast = (byte[]) nodeDataAt(leaf, idx);
          final Object newLast;
          if (decodeIsUtf8(lastSizeAndType)) newLast = Util.codePointAt(oldLast, 0);
          else newLast = oldLast[0];
          return nodeCopyReplaceLast(leaf, newMeta, newLast);
        }
        default: {
          final boolean utf8 = decodeIsUtf8(lastSizeAndType);
          final byte[] newMeta = metaCopyReplaceLast(leafMeta, encodeSizeAndType(lastSize - 1, utf8));
          final byte[] oldLast = (byte[]) nodeDataAt(leaf, idx);
          final int newLastLen = utf8 ? offsetUtf8(oldLast, lastSize - 1) : lastSize - 1;
          final byte[] newLast = new byte[newLastLen];
          System.arraycopy(oldLast, 0, newLast, 0, newLastLen);
          return nodeCopyReplaceLast(leaf, newMeta, newLast);
        }
      }
    } else return nodeCopyRemoveLast(leaf, leafMeta);
  }

  private static Object[] treeRemoveLast(final Object[] tree) {
    final byte treeDepth = metaDepth(nodeMeta(tree));
    Object[] lastChild = (Object[]) nodeDataAt(tree, nodeLength(tree) - 1);
    if (treeDepth == 1) lastChild = leafRemoveLast(lastChild);
    else lastChild = treeRemoveLast(lastChild);
    if (nodeLength(lastChild) == 0) return treeRemoveLastTerminal(tree);
    return treeReplaceLast(tree, lastChild);
  }

  private static Object[] treeRemoveLastTerminal(final Object[] tree) {
    final byte[] oldMeta = nodeMeta(tree);
    final int idx = nodeLength(tree) - 1;
    final byte[] newMeta;
    if (metaTestBit(oldMeta, idx)) {
      newMeta = metaCopyRemoveLast(oldMeta);
      metaClearBit(newMeta, idx);
    } else newMeta = oldMeta;
    return nodeCopyRemoveLast(tree, newMeta);
  }

  public Object first() {
    return lookup(0, null);
  }

  public Object last() {
    return lookup(length() - 1, null);
  }

  public Object lookup(final long index, final com.oracle.truffle.api.nodes.Node node) {
    if (index < 0) throw new BadArgException(String.format(IOOB_MSG, index), node);
    long i = index;
    if (i < prefixSize) return lookupLeaf(prefix, i);
    i -= prefixSize;
    if (i < rootSize) return lookupTree(root, i);
    i -= rootSize;
    if (i < suffixSize) return lookupLeaf(suffix, i);
    throw new BadArgException(String.format(IOOB_MSG, index), node);
  }

  private static Object lookupLeaf(final Object[] leaf, long idx) {
    final byte[] meta = nodeMeta(leaf);
    if (metaIsEmpty(meta)) return nodeDataAt(leaf, (int) idx);
    int j = 0;
    for (int i = 0; i < nodeLength(leaf); i++) {
      if (metaTestBit(meta, i)) {
        final long sizeAndType = metaDataAt(meta, j++);
        final int size = (int) decodeSize(sizeAndType);
        if (idx < size) {
          final byte[] bytes = (byte[]) nodeDataAt(leaf, i);
          if (decodeIsUtf8(sizeAndType)) return Util.codePointAt(bytes, offsetUtf8(bytes, (int) idx));
          else return bytes[(int) idx];
        }
        idx -= size;
      } else {
        if (idx < 1) return nodeDataAt(leaf, i);
        idx--;
      }
    }
    throw new AssertionError();
  }

  private static Object lookupTree(Object[] node, long idx) {
    final byte[] meta = nodeMeta(node);
    byte depth = metaDepth(meta);
    if (depth == 0) return lookupLeaf(node, idx);
    if (metaIsEmpty(meta)) {
      do {
        final long elementSize = elementSizeAtDepth(depth);
        node = (Object[]) nodeDataAt(node, (int) (idx / elementSize));
        idx = idx % elementSize;
        depth--;
      } while (depth != 0);
      return nodeDataAt(node, (int) idx);
    } else {
      int j = 0;
      for (int i = 0; i < nodeLength(node); i++) {
        final long size = metaTestBit(meta, i) ? metaDataAt(meta, j++) : elementSizeAtDepth(depth);
        if (idx < size) return lookupTree((Object[]) nodeDataAt(node, i), idx);
        idx -= size;
      }
    }
    throw new AssertionError();
  }

  public long length() {
    return prefixSize + rootSize + suffixSize;
  }

  public static Seq catenate(final Seq left, final Seq right) {
    final byte leftRootDepth = metaDepth(nodeMeta(left.root));
    Object[] leftTree;
    if (nodeLength(left.suffix) != 0) {
      leftTree = tryTreeInsertLast(left.root, left.suffix);
      if (leftTree == null) leftTree = newLevel(left.root, wrap(left.suffix, leftRootDepth));
    } else leftTree = left.root;
    final byte rightRootDepth = metaDepth(nodeMeta(right.root));
    Object[] rightTree;
    if (nodeLength(right.prefix) != 0) {
      rightTree = tryTreeInsertFirst(right.root, right.prefix);
      if (rightTree == null) rightTree = newLevel(wrap(right.prefix, rightRootDepth), right.root);
    } else rightTree = right.root;
    final byte newRootDepth = (byte) Math.max(leftRootDepth, rightRootDepth);
    final Object[] newRoot = newLevel(wrap(leftTree, newRootDepth), wrap(rightTree, newRootDepth));
    final long newRootSize = left.rootSize + left.suffixSize + right.prefixSize + right.rootSize;
    return new Seq(left.prefix, left.prefixSize, newRoot, newRootSize, right.suffix, right.suffixSize);
  }

  private static Object[] nodeNew(final byte[] meta) {
    return new Object[]{ meta };
  }

  private static Object[] nodeNew(final byte[] meta, final Object sole) {
    return new Object[]{ meta, sole };
  }

  private static Object[] nodeNew(final byte[] meta, final Object first, final Object second) {
    return new Object[]{ meta, first, second };
  }

  private static Object[] nodeCopyInsertFirst(final Object[] node, final byte[] newMeta, final Object o) {
    final Object[] result = new Object[node.length + 1];
    result[0] = newMeta;
    result[1] = o;
    System.arraycopy(node, 1, result, 2, node.length - 1);
    return result;
  }

  private static Object[] nodeCopyInsertLast(final Object[] node, final byte[] newMeta, final Object o) {
    final Object[] result = new Object[node.length + 1];
    result[0] = newMeta;
    System.arraycopy(node, 1, result, 1, node.length - 1);
    result[node.length] = o;
    return result;
  }

  private static Object[] nodeCopyRemoveFirst(final Object[] node, final byte[] newMeta) {
    final Object[] result = new Object[node.length - 1];
    result[0] = newMeta;
    System.arraycopy(node, 2, result, 1, node.length - 2);
    return result;
  }

  private static Object[] nodeCopyRemoveLast(final Object[] node, final byte[] newMeta) {
    final Object[] result = new Object[node.length - 1];
    result[0] = newMeta;
    System.arraycopy(node, 1, result, 1, node.length - 2);
    return result;
  }

  private static Object[] nodeCopyReplaceFirst(final Object[] node, final byte[] newMeta, final Object o) {
    final Object[] result = node.clone();
    result[0] = newMeta;
    result[1] = o;
    return result;
  }

  private static Object[] nodeCopyReplaceLast(final Object[] node, final byte[] newMeta, final Object o) {
    final Object[] result = node.clone();
    result[0] = newMeta;
    result[result.length - 1] = o;
    return result;
  }

  private static int nodeLength(final Object[] node) {
    return node.length - 1;
  }

  private static byte[] nodeMeta(final Object[] node) {
    return (byte[]) node[0];
  }

  private static Object nodeDataAt(final Object[] node, final int idx) {
    return node[idx + 1];
  }

  private static boolean nodeIsSpecial(final Object[] node) {
    return nodeLength(node) != 16 || !metaIsEmpty(nodeMeta(node));
  }

  private static byte[] metaNew(final byte depth) {
    return new byte[]{ depth, 0, 0 };
  }

  private static byte[] metaNew(final byte depth, final long sole) {
    final byte[] result = new byte[11];
    result[0] = depth;
    Util.int64Write(sole, result, 3);
    return result;
  }

  private static byte[] metaNew(final byte depth, final long first, final long second) {
    final byte[] result = new byte[19];
    result[0] = depth;
    Util.int64Write(first, result, 3);
    Util.int64Write(second, result, 11);
    return result;
  }

  private static byte[] metaCopy(final byte[] meta) {
    return meta.clone();
  }

  private static byte[] metaCopyInsertFirst(final byte[] meta, final long value) {
    final byte[] result = new byte[meta.length + 8];
    System.arraycopy(meta, 0, result, 0, 3);
    Util.int64Write(value, result, 3);
    System.arraycopy(meta, 3, result, 11, meta.length - 3);
    return result;
  }

  private static byte[] metaCopyInsertLast(final byte[] meta, final long value) {
    final byte[] result = new byte[meta.length + 8];
    System.arraycopy(meta, 0, result, 0, meta.length);
    Util.int64Write(value, result, meta.length);
    return result;
  }

  private static byte[] metaCopyRemoveFirst(final byte[] meta) {
    final byte[] result = new byte[meta.length - 8];
    System.arraycopy(meta, 0, result, 0, 3);
    System.arraycopy(meta, 11, result, 3, meta.length - 11);
    return result;
  }

  private static byte[] metaCopyRemoveLast(final byte[] meta) {
    final byte[] result = new byte[meta.length - 8];
    System.arraycopy(meta, 0, result, 0, meta.length - 8);
    return result;
  }

  private static byte[] metaCopyReplaceFirst(final byte[] meta, final long value) {
    final byte[] result = meta.clone();
    Util.int64Write(value, result, 3);
    return result;
  }

  private static byte[] metaCopyReplaceLast(final byte[] meta, final long value) {
    final byte[] result = meta.clone();
    Util.int64Write(value, result, meta.length - 8);
    return result;
  }

  private static byte metaDepth(final byte[] meta) {
    return meta[0];
  }

  private static long metaDataAt(final byte[] meta, final int idx) {
    return Util.int64Read(meta, 3 + idx * 8);
  }

  private static boolean metaTestBit(final byte[] meta, final int idx) {
    return ((Util.int16Read(meta, 1) << idx) & 0x8000) != 0;
  }

  private static void metaSetBit(final byte[] meta, final int idx) {
    short bitmap = Util.int16Read(meta, 1);
    bitmap = (short) ((short) (0x8000 >>> idx) | (bitmap & 0xffff));
    Util.int16Write(bitmap, meta, 1);
  }

  private static boolean metaIsEmpty(final byte[] meta) {
    return Util.int16Read(meta, 1) == 0;
  }

  private static void metaClearBit(final byte[] meta, final int idx) {
    short bitmap = Util.int16Read(meta, 1);
    bitmap = (short) ((short) ~(0x8000 >>> idx) & (bitmap & 0xffff));
    Util.int16Write(bitmap, meta, 1);
  }

  private static void metaShiftLeft(final byte[] meta) {
    short bitmap = Util.int16Read(meta, 1);
    bitmap = (short) ((bitmap & 0xffff) << 1);
    Util.int16Write(bitmap, meta, 1);
  }

  private static void metaShiftRight(final byte[] meta) {
    short bitmap = Util.int16Read(meta, 1);
    bitmap = (short) ((bitmap & 0xffff) >>> 1);
    Util.int16Write(bitmap, meta, 1);
  }

  private static long elementSizeAtDepth(final byte depth) {
    return 1L << (depth << 2);
  }

  static int offsetUtf8(final byte[] bytes, int idx) {
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
  }

  private static long encodeSizeAndType(final long size, final boolean utf8) {
    return size | (utf8 ? 0x8000000000000000L : 0x0L);
  }

  private static long decodeSize(final long sizeAndType) {
    return sizeAndType & 0x7fffffffffffffffL;
  }

  private static boolean decodeIsUtf8(final long sizeAndType) {
    return (sizeAndType & 0x8000000000000000L) != 0;
  }
}
