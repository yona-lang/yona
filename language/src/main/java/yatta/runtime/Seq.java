package yatta.runtime;

import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.exceptions.BadArgException;

import java.lang.reflect.Array;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.StandardCharsets;
import java.util.PrimitiveIterator;

public final class Seq {
  static final String IOOB_MSG = "Index out of bounds: %d";
  static final String EMPTY_MSG = "Empty seq";
  static final Object[] EMPTY_NODE = new Object[1];
  static final int BITS = 6;
  static final int MAX_NODE_LENGTH = 64;
  static final int MIN_NODE_LENGTH = 63;
  static final int MASK = 0x3f;

  public static final Seq EMPTY = new Seq(EMPTY_NODE, 0, EMPTY_NODE, 0L, EMPTY_NODE, 0, BITS);

  final byte prefixSize;
  final long rootSize;
  final byte suffixSize;
  final Object[] prefix;
  final Object[] root;
  final Object[] suffix;
  final byte shift;

  long hash = -1;

  Seq(final Object[] prefix, final int prefixSize,
         final Object[] root, final long rootSize,
         final Object[] suffix, final int suffixSize,
         final int shift) {
    this.prefixSize = (byte) prefixSize;
    this.rootSize = rootSize;
    this.suffixSize = (byte) suffixSize;
    this.prefix = prefix;
    this.root = root;
    this.suffix = suffix;
    this.shift = (byte) shift;
  }

  public Seq insertFirst(final Object o) {
    if (prefixSize != MAX_NODE_LENGTH) {
      return new Seq(leafInsertFirst(prefix, o), prefixSize + 1, root, rootSize, suffix, suffixSize, shift);
    }
    Object[] newRoot = treeTryInsertFirst(root, prefix, shift);
    int newShift = shift;
    if (newRoot == null) {
      newRoot = newNonLeaf(wrap(prefix, shift), root, shift);
      newShift += BITS;
    }
    return new Seq(newLeaf(o), 1, newRoot, rootSize + prefixSize, suffix, suffixSize, newShift);
  }

  static Object[] treeTryInsertFirst(final Object[] tree, final Object leaf, final int shift) {
    if (shift == BITS) {
      return tryNonLeafInsertFirst(tree, leaf, 0);
    }
    Object[] child = (Object[]) nodeFirst(tree);
    child = treeTryInsertFirst(child, leaf, shift - BITS);
    if (child != null) {
      return nonLeafReplaceFirst(tree, child, shift - BITS);
    } else if (nodeLength(tree) != MAX_NODE_LENGTH) {
      return tryNonLeafInsertFirst(tree, wrap(leaf, shift - BITS), shift - BITS);
    } else {
      return null;
    }
  }

  static Object[] tryNonLeafInsertFirst(final Object[] parent, final Object child, final int childShift) {
    return nodeLength(parent) == MAX_NODE_LENGTH ? null : nonLeafInsertFirst(parent, child, childShift);
  }

  public Seq insertLast(final Object o) {
    if (suffixSize != MAX_NODE_LENGTH) {
      return new Seq(prefix, prefixSize, root, rootSize, leafInsertLast(suffix, o), suffixSize + 1, shift);
    }
    Object[] newRoot = treeTryInsertLast(root, suffix, shift);
    int newShift = shift;
    if (newRoot == null) {
      newRoot = newNonLeaf(root, wrap(suffix, shift), shift);
      newShift += BITS;
    }
    return new Seq(prefix, prefixSize, newRoot, rootSize + suffixSize, newLeaf(o), 1, newShift);
  }

  static Object[] treeTryInsertLast(final Object[] tree, final Object leaf, final int shift) {
    if (shift == BITS) {
      return tryNonLeafInsertLast(tree, leaf, 0);
    }
    Object[] child = (Object[]) nodeLast(tree);
    child = treeTryInsertLast(child, leaf, shift - BITS);
    if (child != null) {
      return nonLeafReplaceLast(tree, child, shift - BITS);
    } else if (nodeLength(tree) != MAX_NODE_LENGTH) {
      return tryNonLeafInsertLast(tree, wrap(leaf, shift - BITS), shift - BITS);
    } else {
      return null;
    }
  }

  static Object[] tryNonLeafInsertLast(final Object[] parent, final Object child, final int childShift) {
    return nodeLength(parent) == MAX_NODE_LENGTH ? null : nonLeafInsertLast(parent, child, childShift);
  }

  public Seq removeFirst(final Node caller) {
    if (prefixSize != 0) {
      return new Seq(leafRemoveFirst(prefix), prefixSize - 1, root, rootSize, suffix, suffixSize, shift);
    }
    if (rootSize != 0) {
      final FirstAndRest separated = treeSeparateFirst(root, shift);
      final Object[] firstLeaf = objectify(separated.first);
      final int firstLeafSize = nodeLength(firstLeaf);
      final Object[] newPrefix = leafRemoveFirst(firstLeaf);
      final int newPrefixSize = firstLeafSize - 1;
      if (shift == BITS) {
        return new Seq(newPrefix, newPrefixSize, separated.rest, rootSize - firstLeafSize, suffix, suffixSize, BITS);
      } else if (nodeLength(separated.rest) == 1) {
        return new Seq(newPrefix, newPrefixSize, (Object[]) nodeFirst(separated.rest), rootSize - firstLeafSize, suffix, suffixSize, shift - BITS);
      } else {
        return new Seq(newPrefix, newPrefixSize, separated.rest, rootSize - firstLeafSize, suffix, suffixSize, shift);
      }
    }
    if (suffixSize != 0) {
      return new Seq(EMPTY_NODE, 0, EMPTY_NODE, 0L, leafRemoveFirst(suffix), suffixSize - 1, BITS);
    }
    throw new BadArgException(EMPTY_MSG, caller);
  }

  static FirstAndRest treeSeparateFirst(final Object[] tree, int shift) {
    if (shift == BITS) {
      return new FirstAndRest(nodeFirst(tree), nonLeafRemoveFirst(tree, 0));
    }
    shift -= BITS;
    final Object[] oldChild = (Object[]) nodeFirst(tree);
    final FirstAndRest separated = treeSeparateFirst(oldChild, shift);
    final Object[] newChild = separated.rest;
    return new FirstAndRest(separated.first, nodeLength(newChild) == 0 ? nonLeafRemoveFirst(tree, shift) : nonLeafReplaceFirst(tree, newChild, shift));
  }

  static final class FirstAndRest {
    final Object first;
    final Object[] rest;

    FirstAndRest(final Object first, final Object[] rest) {
      this.first = first;
      this.rest = rest;
    }
  }

  public Seq removeLast(final Node caller) {
    if (suffixSize != 0) {
      return new Seq(prefix, prefixSize, root, rootSize, leafRemoveLast(suffix), suffixSize - 1, shift);
    }
    if (rootSize != 0) {
      final InitAndLast separated = treeSeparateLast(root, shift);
      final Object[] lastLeaf = objectify(separated.last);
      final int lastLeafSize = nodeLength(lastLeaf);
      final Object[] newSuffix = leafRemoveLast(lastLeaf);
      final int newSuffixSize = lastLeafSize - 1;
      if (shift == BITS) {
        return new Seq(prefix, prefixSize, separated.init, rootSize - lastLeafSize, newSuffix, newSuffixSize, BITS);
      } else if (nodeLength(separated.init) == 1) {
        return new Seq(prefix, prefixSize, (Object[]) nodeLast(separated.init), rootSize - lastLeafSize, newSuffix, newSuffixSize, shift - BITS);
      } else {
        return new Seq(prefix, prefixSize, separated.init, rootSize - lastLeafSize, newSuffix, newSuffixSize, shift);
      }
    }
    if (prefixSize != 0) {
      return new Seq(leafRemoveLast(prefix), prefixSize - 1, EMPTY_NODE, 0L, EMPTY_NODE, 0, BITS);
    }
    throw new BadArgException(EMPTY_MSG, caller);
  }

  static InitAndLast treeSeparateLast(final Object[] tree, int shift) {
    if (shift == BITS) {
      return new InitAndLast(nonLeafRemoveLast(tree, 0), nodeLast(tree));
    }
    shift -= BITS;
    final Object[] oldChild = (Object[]) nodeLast(tree);
    final InitAndLast separated = treeSeparateLast(oldChild, shift);
    final Object[] newChild = separated.init;
    return new InitAndLast(nodeLength(newChild) == 0 ? nonLeafRemoveLast(tree, shift) : nonLeafReplaceLast(tree, newChild, shift), separated.last);
  }

  static final class InitAndLast {
    final Object[] init;
    final Object last;

    InitAndLast(final Object[] init, final Object last) {
      this.init = init;
      this.last = last;
    }
  }

  public Object lookup(final long index, final Node caller) {
    if (index < 0) {
      throw new BadArgException(String.format(IOOB_MSG, index), caller);
    }
    long i = index;
    if (i < prefixSize) {
      return nodeLookup(prefix, (int) i);
    }
    i -= prefixSize;
    if (i < rootSize) {
      return treeLookup(root, i, shift);
    }
    i -= rootSize;
    if (i < suffixSize) {
      return nodeLookup(suffix, (int) i);
    }
    throw new BadArgException(String.format(IOOB_MSG, index), caller);
  }

  static Object treeLookup(Object tree, long i, int shift) {
    for (long[] meta = nodeMeta(tree); meta != null; meta = nodeMeta(tree)) {
      int guess = (int) (i / elementSizeAt(shift));
      while (meta[guess] <= i) {
        guess++;
      }
      if (guess > 0) {
        i -= meta[guess - 1];
      }
      tree = nodeLookup(tree, guess);
      shift -= BITS;
    }
    return treeFastLookup(tree, i, shift);
  }

  static Object treeFastLookup(Object node, final long index, int shift) {
    while (shift > 0) {
      node = nodeLookup(node, (int) ((index >>> shift) & MASK));
      shift -= BITS;
    }
    return nodeLookup(node, (int) (index & MASK));
  }

  public Object foldLeft(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
    Object result = initial;
    for (int i = 0; i < prefixSize; i++) {
      result = dispatch.execute(function, result, nodeLookup(prefix, i));
    }
    result = nodeFoldLeft(root, shift, result, function, dispatch);
    for (int i = 0; i < suffixSize; i++) {
      result = dispatch.execute(function, result, nodeLookup(suffix, i));
    }
    return result;
  }

  public Object foldRight(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
    Object result = initial;
    for (int i = suffixSize - 1; i >= 0; i--) {
      result = dispatch.execute(function, result, nodeLookup(suffix, i));
    }
    result = nodeFoldRight(root, shift, result, function, dispatch);
    for (int i = prefixSize - 1; i >= 0; i--) {
      result = dispatch.execute(function, result, nodeLookup(prefix, i));
    }
    return result;
  }

  public long length() {
    return prefixSize + rootSize + suffixSize;
  }

  public boolean asChars(final CharBuffer buffer) {
    if (!appendCodePoints(buffer, prefix, 0)) {
      return false;
    }
    if (!appendCodePoints(buffer, root, shift)) {
      return false;
    }
    //noinspection RedundantIfStatement
    if (!appendCodePoints(buffer, suffix, 0)) {
      return false;
    }
    return true;
  }

  static boolean appendCodePoints(final CharBuffer buffer, final Object node, final int shift) {
    final int len = nodeLength(node);
    if (shift == 0) {
      if (node instanceof byte[]) {
        final byte[] bytes = (byte[]) node;
        if (!decodeIsUtf8(bytes[0])) {
          return false;
        }
        int offset = 1;
        for (int i = 0; i < len; i++) {
          final int codePoint = Util.codePointAt(bytes, offset);
          appendCodePoint(buffer, codePoint);
          offset += Util.codePointLen(codePoint);
        }
      } else {
        for (int i = 0; i < len; i++) {
          Object o = nodeLookup(node, i);
          if (!(o instanceof Integer)) {
            return false;
          }
          appendCodePoint(buffer, (Integer) o);
        }
      }
    } else {
      for (int i = 0; i < len; i++) {
        if (!appendCodePoints(buffer, nodeLookup(node, i), shift - BITS)) {
          return false;
        }
      }
    }
    return true;
  }

  static void appendCodePoint(final CharBuffer buffer, final int codePoint) {
    if (Character.isBmpCodePoint(codePoint)) {
      buffer.put((char) codePoint);
    } else {
      buffer.put(Character.highSurrogate(codePoint));
      buffer.put(Character.lowSurrogate(codePoint));
    }
  }

  public boolean asBytes(final ByteBuffer buffer) {
    if (!appendBytes(buffer, prefix, 0)) {
      return false;
    }
    if (!appendBytes(buffer, root, shift)) {
      return false;
    }
    //noinspection RedundantIfStatement
    if (!appendBytes(buffer, suffix, 0)) {
      return false;
    }
    return true;
  }

  static boolean appendBytes(final ByteBuffer buffer, final Object node, final int shift) {
    final int len = nodeLength(node);
    if (shift == 0) {
      if (node instanceof byte[]) {
        final byte[] bytes = (byte[]) node;
        buffer.put(bytes, 1, bytes.length - 1);
      } else {
        for (int i = 0; i < len; i++) {
          Object o = nodeLookup(node, i);
          if (o instanceof Integer) {
            buffer.put(new String(new int[]{ (Integer) o }, 0, 1).getBytes(StandardCharsets.UTF_8));
          } else if (o instanceof Byte) {
            buffer.put((Byte) o);
          } else {
            return false;
          }
        }
      }
    } else {
      for (int i = 0; i < len; i++) {
        if (!appendBytes(buffer, nodeLookup(node, i), shift - BITS)) {
          return false;
        }
      }
    }
    return true;
  }

  @Override
  public boolean equals(Object o) {
    if (o == this) {
      return true;
    }
    if (!(o instanceof Seq)) {
      return false;
    }
    Seq that = (Seq) o;
    if (this.length() != that.length()) {
      return false;
    }
    for (int i = 0; i < prefixSize + rootSize + suffixSize; i++) {
      if (!this.lookup(i, null).equals(that.lookup(i, null))) {
        return false;
      }
    }
    return true;
  }

  static Object wrap(final Object leaf, final int shift) {
    return shift == 0 ? leaf : newNonLeaf(wrap(leaf, shift - BITS), shift - BITS);
  }

  static Object[] objectify(final Object leaf) {
    if (leaf instanceof byte[]) {
      final byte[] bytes = (byte[]) leaf;
      final Object[] result = new Object[nodeLength(leaf) + 1];
      if (decodeIsUtf8(bytes[0])) {
        for (int i = 1; i < bytes.length; i++) {
          result[i] = Util.codePointAt(bytes, Util.offsetUtf8(bytes, 1, i - 1));
        }
      } else {
        for (int i = 1; i < bytes.length; i++) {
          result[i] = bytes[i];
        }
      }
      return result;
    } else {
      return (Object[]) leaf;
    }
  }

  static int nodeLength(final Object node) {
    return node instanceof byte[] ? decodeLength(((byte[]) node)[0]) : Array.getLength(node) - 1;
  }

  static long[] nodeMeta(final Object node) {
    return node instanceof Object[] ? (long[]) Array.get(node, 0) : null;
  }

  static boolean nodeIsSpecial(final Object node) {
    return nodeLength(node) != MAX_NODE_LENGTH || nodeMeta(node) != null;
  }

  static long nodeSize(final Object node, final int shift) {
    final long[] meta = nodeMeta(node);
    return meta != null ? meta[meta.length - 1] : nodeLength(node) * elementSizeAt(shift);
  }

  static Object nodeLookup(final Object node, final int i) {
    if (node instanceof byte[]) {
      final byte[] bytes = (byte[]) node;
      if (decodeIsUtf8(bytes[0])) {
        return Util.codePointAt(bytes, Util.offsetUtf8(bytes, 1, i));
      } else {
        return bytes[i + 1];
      }
    } else {
      return ((Object[]) node)[i + 1];
    }
  }

  static Object nodeFirst(final Object node) {
    return nodeLookup(node, 0);
  }

  static Object nodeLast(final Object node) {
    return nodeLookup(node, nodeLength(node) - 1);
  }

  static Object nodeFoldLeft(final Object node, final int shift, final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
    final int len = nodeLength(node);
    Object result = initial;
    if (shift == 0) {
      for (int i = 0; i < len; i++) {
        result = dispatch.execute(function, result, nodeLookup(node, i));
      }
    } else {
      for (int i = 0; i < len; i++) {
        result = nodeFoldLeft(nodeLookup(node, i), shift - BITS, result, function, dispatch);
      }
    }
    return result;
  }

  static Object nodeFoldRight(final Object node, final int shift, final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
    final int len = nodeLength(node);
    Object result = initial;
    if (shift == 0) {
      for (int i = len - 1; i >= 0; i--) {
        result = dispatch.execute(function, result, nodeLookup(node, i));
      }
    } else {
      for (int i = len - 1; i >= 0; i--) {
        result = nodeFoldRight(nodeLookup(node, i), shift - BITS, result, function, dispatch);
      }
    }
    return result;
  }

  static Object[] newLeaf(final Object value) {
    return new Object[]{ null, value };
  }

  static byte[] newLeaf(final ByteSource source, final int n) {
    final byte[] result = source.next(1, n);
    result[0] = encode(n, false);
    return result;
  }

  static byte[] newLeaf(final Utf8Source source, final int n) {
    final byte[] result = source.next(1, n);
    result[0] = encode(n, true);
    return result;
  }

  static Object[] leafInsertFirst(final Object[] leaf, final Object value) {
    final Object[] result = new Object[leaf.length + 1];
    result[1] = value;
    System.arraycopy(leaf, 1, result, 2, leaf.length - 1);
    return result;
  }

  static Object[] leafInsertLast(final Object[] leaf, final Object value) {
    final Object[] result = new Object[leaf.length + 1];
    System.arraycopy(leaf, 1, result, 1, leaf.length - 1);
    result[leaf.length] = value;
    return result;
  }

  static Object[] leafRemoveFirst(final Object[] leaf) {
    if (leaf.length == 2) {
      return EMPTY_NODE;
    }
    final Object[] result = new Object[leaf.length - 1];
    System.arraycopy(leaf, 2, result, 1, leaf.length - 2);
    return result;
  }

  static Object[] leafRemoveLast(final Object[] leaf) {
    if (leaf.length == 2) {
      return EMPTY_NODE;
    }
    final Object[] result = new Object[leaf.length - 1];
    System.arraycopy(leaf, 1, result, 1, leaf.length - 2);
    return result;
  }

  static Object[] newNonLeaf(final Object child, final int childShift) {
    long[] meta = null;
    if (nodeIsSpecial(child)) {
      meta = newMeta(nodeSize(child, childShift));
    }
    return new Object[]{ meta, child };
  }

  static Object[] newNonLeaf(final Object firstChild, final Object secondChild, final int childShift) {
    long[] meta = null;
    if (nodeIsSpecial(firstChild) || nodeIsSpecial(secondChild)) {
      meta = newMeta(nodeSize(firstChild, childShift), nodeSize(secondChild, childShift));
    }
    return new Object[]{ meta, firstChild, secondChild };
  }

  static Object[] nonLeafInsertFirst(final Object[] parent, final Object child, final int childShift) {
    final Object[] result = new Object[parent.length + 1];
    final long[] parentMeta = nodeMeta(parent);
    if (parentMeta != null) {
      result[0] = metaInsertFirst(parentMeta, nodeSize(child, childShift));
    } else if (nodeIsSpecial(child)) {
      result[0] = newMetaWithFirst(nodeSize(child, childShift), elementSizeAt(childShift + BITS), nodeLength(parent));
    }
    result[1] = child;
    System.arraycopy(parent, 1, result, 2, parent.length - 1);
    return result;
  }

  static Object[] nonLeafInsertLast(final Object[] parent, final Object child, final int childShift) {
    final Object[] result = new Object[parent.length + 1];
    final long[] parentMeta = nodeMeta(parent);
    if (parentMeta != null) {
      result[0] = metaInsertLast(parentMeta, nodeSize(child, childShift));
    } else if (nodeIsSpecial(child)) {
      result[0] = newMetaWithLast(elementSizeAt(childShift + BITS), nodeLength(parent), nodeSize(child, childShift));
    }
    System.arraycopy(parent, 1, result, 1, parent.length - 1);
    result[result.length - 1] = child;
    return result;
  }

  static Object[] nonLeafReplaceFirst(final Object[] parent, final Object child, final int childShift) {
    final Object[] result = parent.clone();
    final long[] oldMeta = nodeMeta(parent);
    result[0] = null;
    if (oldMeta != null) {
      if (nodeIsSpecial(child) || !metaCanNullifyTail(oldMeta, elementSizeAt(childShift) * MAX_NODE_LENGTH)) {
        result[0] = metaReplaceFirst(oldMeta, nodeSize(child, childShift));
      }
    } else {
      if (nodeIsSpecial(child)) {
        result[0] = newMetaWithFirst(nodeSize(child, childShift), elementSizeAt(childShift + BITS), nodeLength(parent) - 1);
      }
    }
    result[1] = child;
    return result;
  }

  static Object[] nonLeafReplaceLast(final Object[] parent, final Object child, final int childShift) {
    final Object[] result = parent.clone();
    final long[] oldMeta = nodeMeta(parent);
    result[0] = null;
    if (oldMeta != null) {
      if (nodeIsSpecial(child) || !metaCanNullifyInit(oldMeta, elementSizeAt(childShift) * MAX_NODE_LENGTH)) {
        result[0] = metaReplaceLast(oldMeta, nodeSize(child, childShift));
      }
    } else {
      if (nodeIsSpecial(child)) {
        result[0] = newMetaWithLast(elementSizeAt(childShift + BITS), nodeLength(parent) - 1, nodeSize(child, childShift));
      }
    }
    result[result.length - 1] = child;
    return result;
  }

  static Object[] nonLeafRemoveFirst(final Object[] node, final int childShift) {
    if (nodeLength(node) == 1) {
      return EMPTY_NODE;
    }
    final Object[] result = new Object[node.length - 1];
    final long[] oldMeta = nodeMeta(node);
    if (oldMeta != null && !metaCanNullifyTail(oldMeta, elementSizeAt(childShift) * MAX_NODE_LENGTH)) {
      result[0] = metaRemoveFirst(oldMeta);
    }
    System.arraycopy(node, 2, result, 1, node.length - 2);
    return result;
  }

  static Object[] nonLeafRemoveLast(final Object[] node, final int childShift) {
    if (nodeLength(node) == 1) {
      return EMPTY_NODE;
    }
    final Object[] result = new Object[node.length - 1];
    final long[] oldMeta = nodeMeta(node);
    if (oldMeta != null && !metaCanNullifyInit(oldMeta, elementSizeAt(childShift) * MAX_NODE_LENGTH)) {
      result[0] = metaRemoveLast(oldMeta);
    }
    System.arraycopy(node, 1, result, 1, node.length - 2);
    return result;
  }

  static long[] newMeta(final long sole) {
    return new long[]{ sole };
  }

  static long[] newMeta(final long first, final long second) {
    return new long[]{ first, first + second };
  }

  static long[] newMetaWithFirst(final long first, final long rest, final int n) {
    final long[] result = new long[n + 1];
    result[0] = first;
    for (int i = 1; i < result.length; i++) {
      result[i] = first + i * rest;
    }
    return result;
  }

  static long[] newMetaWithLast(final long rest, final int n, final long last) {
    final long[] result = new long[n + 1];
    long c = 0;
    for (int i = 0; i < result.length - 1; i++) {
      c += rest;
      result[i] = c;
    }
    result[result.length - 1] = last + c;
    return result;
  }

  static long[] metaInsertFirst(final long[] meta, final long value) {
    final long[] result = new long[meta.length + 1];
    result[0] = value;
    for (int i = 0; i < meta.length; i++) {
      result[i + 1] = meta[i] + value;
    }
    return result;
  }

  static long[] metaInsertLast(final long[] meta, final long value) {
    final long[] result = new long[meta.length + 1];
    System.arraycopy(meta, 0, result, 0, meta.length);
    result[meta.length] = result[meta.length - 1] + value;
    return result;
  }

  static long[] metaReplaceFirst(final long[] meta, final long value) {
    final long[] result = new long[meta.length];
    final long old = meta[0];
    for (int i = 0; i < meta.length; i++) {
      result[i] = meta[i] - old + value;
    }
    return result;
  }

  static long[] metaReplaceLast(final long[] meta, final long value) {
    if (meta.length == 1) {
      return new long[]{ value };
    }
    final long[] result = meta.clone();
    result[result.length - 1] = result[result.length - 2] + value ;
    return result;
  }

  static long[] metaRemoveFirst(final long[] meta) {
    if (meta.length == 1) {
      return null;
    }
    final long[] result = new long[meta.length - 1];
    final long first = meta[0];
    for (int i = 1; i < meta.length; i++) {
      result[i - 1] = meta[i] - first;
    }
    return result;
  }

  static long[] metaRemoveLast(final long[] meta) {
    if (meta.length == 1) {
      return null;
    }
    final long[] result = new long[meta.length - 1];
    System.arraycopy(meta, 0, result, 0, result.length);
    return result;
  }

  static boolean metaCanNullifyTail(final long[] meta, final long expectedSize) {
    if (meta.length == 1) {
      return true;
    }
    final long first = meta[0];
    for (int i = 1; i < meta.length; i++) {
      if (meta[i] != expectedSize * i + first) {
        return false;
      }
    }
    return true;
  }

  static boolean metaCanNullifyInit(final long[] meta, final long expectedSize) {
    if (meta.length == 1) {
      return true;
    }
    for (int i = 0; i < meta.length - 1; i++) {
      if (meta[i] != expectedSize * (i + 1)) {
        return false;
      }
    }
    return true;
  }

  static byte encode(final int length, final boolean isUtf8) {
    return (byte) ((length & 0x7f) | (isUtf8 ? 0x80 : 0x0));
  }

  static int decodeLength(final byte encoded) {
    return encoded & 0x7f;
  }

  static boolean decodeIsUtf8(final byte encoded) {
    return (encoded & 0x80) != 0;
  }

  static long elementSizeAt(final int shift) {
    return 1L << shift;
  }

  public static Seq fromBytes(final ByteSource source) {
    int shift = BITS;
    Object[] root = EMPTY_NODE;
    for (int remaining = source.remaining(); remaining / MAX_NODE_LENGTH != 0; remaining -= MAX_NODE_LENGTH) {
      byte[] leaf = newLeaf(source, MAX_NODE_LENGTH);
      Object[] newRoot = treeTryInsertLast(root, leaf, shift);
      if (newRoot == null) {
        newRoot = newNonLeaf(root, wrap(leaf, shift), shift);
        shift += BITS;
      }
      root = newRoot;
    }
    Object[] suffix = objectify(newLeaf(source, source.remaining() % MAX_NODE_LENGTH));
    return new Seq(EMPTY_NODE, 0, root, nodeSize(root, shift), suffix, nodeLength(suffix), shift);
  }

  public static abstract class ByteSource {
    abstract int remaining();

    abstract byte[] next(final int offset, final int n);
  }

  public static Seq fromUtf8(final Utf8Source source) {
    int shift = BITS;
    Object[] root = EMPTY_NODE;
    for (int remaining = source.remaining(); remaining / MAX_NODE_LENGTH != 0; remaining -= MAX_NODE_LENGTH) {
      byte[] leaf = newLeaf(source, MAX_NODE_LENGTH);
      Object[] newRoot = treeTryInsertLast(root, leaf, shift);
      if (newRoot == null) {
        newRoot = newNonLeaf(root, wrap(leaf, shift), shift);
        shift += BITS;
      }
      root = newRoot;
    }
    Object[] suffix = objectify(newLeaf(source, source.remaining() % MAX_NODE_LENGTH));
    return new Seq(EMPTY_NODE, 0, root, nodeSize(root, shift), suffix, nodeLength(suffix), shift);
  }

  public static abstract class Utf8Source {
    abstract int remaining();

    abstract byte[] next(final int offset, final int n);
  }

  public static Seq fromCharSequence(final CharSequence source) {
    Seq result = Seq.EMPTY;
    final PrimitiveIterator.OfInt iterator = source.codePoints().iterator();
    while (iterator.hasNext()) {
      result = result.insertLast(iterator.next());
    }
    return result;
  }
}
