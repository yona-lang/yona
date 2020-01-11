package yatta.runtime;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.exceptions.BadArgException;

import java.lang.reflect.Array;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.util.function.BiFunction;

@ExportLibrary(InteropLibrary.class)
public final class Seq implements TruffleObject {
  static final String IOOB_MSG = "Index out of bounds: %d";
  static final String EMPTY_MSG = "Empty seq";
  static final Object[] EMPTY_NODE = new Object[1];
  static final int BITS = 6;
  static final int MASK = 0x3f;
  static final int MAX_NODE_LENGTH = 64;
  static final int MIN_NODE_LENGTH = 63;

  public static final Seq EMPTY = new Seq(EMPTY_NODE, 0, EMPTY_NODE, 0L, EMPTY_NODE, 0, BITS);

  final byte prefixSize;
  final long rootSize;
  final byte suffixSize;
  final Object[] prefix;
  final Object[] root;
  final Object[] suffix;
  final byte shift;

  volatile long hash = 0L;

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

  @ExportMessage
  public final long getArraySize() {
    return length();
  }

  @ExportMessage
  public final Object readArrayElement(long index) {
    return lookup(index, null);
  }

  @ExportMessage
  public final boolean isArrayElementReadable(long index) {
    return index < length();
  }

  @ExportMessage
  public final boolean hasArrayElements() {
    return true;
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  public boolean isString() {
    CompilerAsserts.compilationConstant(length());
    for (long i = 0; i < length(); i++) {
      if (!(lookup(i, null) instanceof Integer)) {
        return false;
      }
    }
    return true;
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  public String asString() {
    return asJavaString(null);
  }

  static boolean isInstance(TruffleObject seq) {
    return seq instanceof Seq;
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public static Seq sequence(Object... values) {
    Seq result = EMPTY;
    for (Object value : values) {
      result = result.insertLast(value);
    }
    return result;
  }

  @CompilerDirectives.TruffleBoundary
  public boolean contains(Object element, final Node caller) {
    CompilerAsserts.compilationConstant(length());
    for (long i = 0; i < length(); i++) {
      if (element.equals(lookup(i, null))) {
        return true;
      }
    }
    return false;
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Object first(Node caller) {
    return lookup(0, caller);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Object last(Node caller) {
    return lookup(length() - 1, caller);
  }

  @Override
  @CompilerDirectives.TruffleBoundary
  public String toString() {
    if (!isString()) {
      StringBuilder sb = new StringBuilder();
      sb.append("[");

      for (int i = 0; i < length(); i++) {
        sb.append(lookup(i, null));

        if (i != length() - 1) {
          sb.append(", ");
        }
      }

      sb.append("]");

      return sb.toString();
    } else {
      return asJavaString(null);
    }
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq insertFirst(final Object o) {
    if (prefixSize != MAX_NODE_LENGTH) {
      return new Seq(leafInsertFirst(prefix, o), prefixSize + 1, root, rootSize, suffix, suffixSize, shift);
    }
    Object[] newRoot = treeTryInsertFirst(root, prefix, shift);
    int newShift = shift;
    if (newRoot == null) {
      newRoot = newNonLeaf(wrap(prefix, 0, shift), root, shift);
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
      return tryNonLeafInsertFirst(tree, wrap(leaf, 0, shift - BITS), shift - BITS);
    } else {
      return null;
    }
  }

  static Object[] tryNonLeafInsertFirst(final Object[] parent, final Object child, final int childShift) {
    return nodeLength(parent) == MAX_NODE_LENGTH ? null : nonLeafInsertFirst(parent, child, childShift);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq insertLast(final Object o) {
    if (suffixSize != MAX_NODE_LENGTH) {
      return new Seq(prefix, prefixSize, root, rootSize, leafInsertLast(suffix, o), suffixSize + 1, shift);
    }
    Object[] newRoot = treeTryInsertLast(root, suffix, shift);
    int newShift = shift;
    if (newRoot == null) {
      newRoot = newNonLeaf(root, wrap(suffix, 0, shift), shift);
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
      return tryNonLeafInsertLast(tree, wrap(leaf, 0, shift - BITS), shift - BITS);
    } else {
      return null;
    }
  }

  static Object[] tryNonLeafInsertLast(final Object[] parent, final Object child, final int childShift) {
    return nodeLength(parent) == MAX_NODE_LENGTH ? null : nonLeafInsertLast(parent, child, childShift);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
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
      } else if (nodeLength(separated.rest) == 0) {
        return new Seq(newPrefix, newPrefixSize, EMPTY_NODE, 0, suffix, suffixSize, BITS);
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

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
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
      } else if (nodeLength(separated.init) == 0) {
        return new Seq(prefix, prefixSize, EMPTY_NODE, 0, newSuffix, newSuffixSize, BITS);
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

  Object[] splitAt(final long index, final Node caller) {
    if (index < 0) {
      throw new BadArgException(String.format(IOOB_MSG, index), caller);
    }
    long i = index;
    if (i < prefixSize) {
      final Object[] pfxSplit = nodeSplitAt(prefix, (int) i, 0);
      final Object[] pfxSplitLeft = (Object[]) pfxSplit[0];
      final Object pfxSplitMedium = pfxSplit[1];
      final Object[] pfxSplitRight = (Object[]) pfxSplit[2];
      final Seq left = new Seq(EMPTY_NODE, 0, EMPTY_NODE, 0, pfxSplitLeft, nodeLength(pfxSplitLeft), BITS);
      final Seq right = new Seq(pfxSplitRight, nodeLength(pfxSplitRight), root, rootSize, suffix, suffixSize, shift);
      return new Object[]{ left, pfxSplitMedium, right };
    }
    i -= prefixSize;
    if (i < rootSize) {
      final Object[] treeSplit = treeSplitAt(root, i, shift);
      Object[] leftRoot = (Object[]) treeSplit[0];
      final Object medium = treeSplit[1];
      Object[] rightRoot = (Object[]) treeSplit[2];
      int leftShift = shift;
      int rightShift = shift;
      final Object[] leftSuffix;
      if (nodeLength(leftRoot) == 0) {
        leftShift = BITS;
        leftSuffix = EMPTY_NODE;
      } else {
        final InitAndLast leftRootAndSuffix = treeSeparateLast(leftRoot, leftShift);
        leftRoot = leftRootAndSuffix.init;
        leftSuffix = objectify(leftRootAndSuffix.last);
        if (nodeLength(leftRoot) == 0) {
          leftShift = BITS;
        }
      }
      final Object[] rightPrefix;
      if (nodeLength(rightRoot) == 0) {
        rightShift = BITS;
        rightPrefix = EMPTY_NODE;
      } else {
        final FirstAndRest rightPrefixAndRoot = treeSeparateFirst(rightRoot, rightShift);
        rightPrefix = objectify(rightPrefixAndRoot.first);
        rightRoot = rightPrefixAndRoot.rest;
        if (nodeLength(rightRoot) == 0) {
          rightShift = BITS;
        }
      }
      while (leftShift > BITS && nodeLength(leftRoot) == 1) {
        leftRoot = (Object[]) nodeFirst(leftRoot);
        leftShift -= BITS;
      }
      while (rightShift > BITS && nodeLength(rightRoot) == 1) {
        rightRoot = (Object[]) nodeFirst(rightRoot);
        rightShift -= BITS;
      }
      final Seq left = new Seq(prefix, prefixSize, leftRoot, nodeSize(leftRoot, leftShift), leftSuffix, nodeLength(leftSuffix), leftShift);
      final Seq right = new Seq(rightPrefix, nodeLength(rightPrefix), rightRoot, nodeSize(rightRoot, rightShift), suffix, suffixSize, rightShift);
      return new Object[]{ left, medium, right };
    }
    i -= rootSize;
    if (i < suffixSize) {
      final Object[] sfxSplit = nodeSplitAt(suffix, (int) i, 0);
      final Object[] sfxSplitLeft = (Object[]) sfxSplit[0];
      final Object sfxSplitMedium = sfxSplit[1];
      final Object[] sfxSplitRight = (Object[]) sfxSplit[2];
      final Seq left = new Seq(prefix, prefixSize, root, rootSize, sfxSplitLeft, nodeLength(sfxSplitLeft), shift);
      final Seq right = new Seq(sfxSplitRight, nodeLength(sfxSplitRight), EMPTY_NODE, 0, EMPTY_NODE, 0, BITS);
      return new Object[]{ left, sfxSplitMedium, right };
    }
    throw new BadArgException(String.format(IOOB_MSG, index), caller);
  }

  Object[] treeSplitAt(final Object tree, long idx, final int shift) {
    final long[] meta = nodeMeta(tree);
    final int i;
    if (meta == null) {
      i = (int) ((idx >>> shift) & MASK);
    } else {
      int guess = (int) (idx / elementSizeAt(shift));
      while (meta[guess] <= idx) {
        guess++;
      }
      if (guess != 0) {
        idx -= meta[guess - 1];
      }
      i = guess;
    }
    final Object[] pt = nodeSplitAt(tree, i, shift);
    if (shift == 0) {
      return pt;
    } else {
      Object[] leftParent = (Object[]) pt[0];
      Object[] rightParent = (Object[]) pt[2];
      final Object[] subPt = treeSplitAt(pt[1], idx, shift - BITS);
      final Object leftChild = subPt[0];
      final Object rightChild = subPt[2];
      if (nodeLength(leftChild) > 0) {
        leftParent = nonLeafInsertLast(leftParent, leftChild, shift - BITS);
      }
      if (nodeLength(rightChild) > 0) {
        rightParent = nonLeafInsertFirst(rightParent, rightChild, shift - BITS);
      }
      return new Object[]{ leftParent, subPt[1], rightParent };
    }
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
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

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq[] split(final long idx, final Node caller) {
    final Object[] pt = splitAt(idx, caller);
    return new Seq[]{ (Seq) pt[0], ((Seq) pt[2]).insertFirst(pt[1]) };
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq take(final long n, final Node caller) {
    final Object[] pt = splitAt(n, caller);
    return (Seq) pt[0];
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq drop(final long n, final Node caller) {
    final Object[] pt = splitAt(n, caller);
    return ((Seq) pt[2]).insertFirst(pt[1]);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq replace(final long idx, final Object value, final Node caller) {
    final Object[] pt = splitAt(idx, caller);
    return catenate(((Seq) pt[0]).insertLast(value), (Seq) pt[2]);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq remove(final long idx, final Node caller) {
    final Object[] pt = splitAt(idx, caller);
    return catenate((Seq) pt[0], (Seq) pt[2]);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
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

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public <T> T foldLeft(final T initial, final BiFunction<T, Object, T> function) {
    T result = initial;
    for (int i = 0; i < prefixSize; i++) {
      result = function.apply(result, nodeLookup(prefix, i));
    }
    result = nodeFoldLeft(root, shift, result, function);
    for (int i = 0; i < suffixSize; i++) {
      result = function.apply(result, nodeLookup(suffix, i));
    }
    return result;
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
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

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public <T> T foldRight(final T initial, final BiFunction<T, Object, T> function) {
    T result = initial;
    for (int i = suffixSize - 1; i >= 0; i--) {
      result = function.apply(result, nodeLookup(suffix, i));
    }
    result = nodeFoldRight(root, shift, result, function);
    for (int i = prefixSize - 1; i >= 0; i--) {
      result = function.apply(result, nodeLookup(prefix, i));
    }
    return result;
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public Seq map(final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
    return new Seq(nodeMap(prefix, 0, function, dispatch), prefixSize, nodeMap(root, shift, function, dispatch), rootSize, nodeMap(suffix, 0, function, dispatch), suffixSize, shift);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
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

  public String asJavaString(Node caller) {
    long len = length();
    if (len > Integer.MAX_VALUE / 2) {
      throw new BadArgException("Sequence too long to be converted to Java String", caller);
    }
    CharBuffer charBuffer = CharBuffer.allocate((int) len * 2);
    if (asChars(charBuffer)) {
      charBuffer.limit(charBuffer.position());
      charBuffer.position(0);
      return charBuffer.toString();
    } else {
      throw new BadArgException("Unable to convert sequence to Java String", caller);
    }
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
          final int codePoint = Util.utf8Decode(bytes, offset);
          appendCodePoint(buffer, codePoint);
          offset += Util.utf8Length(codePoint);
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
            Util.utf8Encode(buffer, (Integer) o);
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

  long murmur3Hash(final long seed) {
    if (seed == 0L) {
      if (hash == 0L) {
        hash = calculateMurmur3Hash(0L);
      }
      return hash;
    } else {
      return calculateMurmur3Hash(seed);
    }
  }

  long calculateMurmur3Hash(final long seed) {
    long hash = seed;
    final long length = length();
    for (int i = 0; i < length; i++) {
      long k = Murmur3.INSTANCE.hash(seed, lookup(i, null));
      k *= Murmur3.C1;
      k = Long.rotateLeft(k, 31);
      k *= Murmur3.C2;
      hash ^= k;
      hash = Long.rotateLeft(hash, 27) * 5 + 0x52dce729;
    }
    return Murmur3.fMix64(hash ^ length);
  }

  @Override
  public int hashCode() {
    return (int) murmur3Hash(0);
  }

  @Override
  @CompilerDirectives.TruffleBoundary(allowInlining = true)
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
    final long length = length();
    for (long i = 0; i < length; i++) {
      if (!this.lookup(i, null).equals(that.lookup(i, null))) {
        return false;
      }
    }
    return true;
  }

  static Object wrap(final Object node, final int nodeShift, int desiredShift) {
    if (nodeShift == desiredShift) {
      return node;
    } else {
      desiredShift -= BITS;
      return newNonLeaf(wrap(node, nodeShift, desiredShift), desiredShift);
    }
  }

  static Object[] objectify(final Object node) {
    if (node instanceof byte[]) {
      final byte[] bytes = (byte[]) node;
      final Object[] result = new Object[nodeLength(node) + 1];
      if (decodeIsUtf8(bytes[0])) {
        for (int i = 1; i < result.length; i++) {
          result[i] = Util.utf8Decode(bytes, Util.utf8Offset(bytes, 1, i - 1));
        }
      } else {
        for (int i = 1; i < result.length; i++) {
          result[i] = bytes[i];
        }
      }
      return result;
    } else {
      return (Object[]) node;
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

  static Object[] nodeSplitAt(final Object node, final int i, final int shift) {
    final Object[] src = objectify(node);
    Object[] left = new Object[i + 1];
    System.arraycopy(src, 1, left, 1, i);
    if (shift != 0) {
      buildIndex(left, shift - BITS);
    }
    final int j = nodeLength(src) - i - 1;
    Object[] right = new Object[j + 1];
    System.arraycopy(src, i + 2, right, 1, j);
    if (shift != 0) {
      buildIndex(right, shift - BITS);
    }
    return new Object[]{ left, src[i + 1], right };
  }

  static Object nodeLookup(final Object node, final int i) {
    if (node instanceof byte[]) {
      final byte[] bytes = (byte[]) node;
      if (decodeIsUtf8(bytes[0])) {
        return Util.utf8Decode(bytes, Util.utf8Offset(bytes, 1, i));
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

  static <T> T nodeFoldLeft(final Object node, final int shift, final T initial, final BiFunction<T, Object, T> function) {
    final int len = nodeLength(node);
    T result = initial;
    if (shift == 0) {
      for (int i = 0; i < len; i++) {
        result = function.apply(result, nodeLookup(node, i));
      }
    } else {
      for (int i = 0; i < len; i++) {
        result = nodeFoldLeft(nodeLookup(node, i), shift - BITS, result, function);
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

  static <T> T nodeFoldRight(final Object node, final int shift, final T initial, final BiFunction<T, Object, T> function) {
    final int len = nodeLength(node);
    T result = initial;
    if (shift == 0) {
      for (int i = len - 1; i >= 0; i--) {
        result = function.apply(result, nodeLookup(node, i));
      }
    } else {
      for (int i = len - 1; i >= 0; i--) {
        result = nodeFoldRight(nodeLookup(node, i), shift - BITS, result, function);
      }
    }
    return result;
  }

  static Object[] nodeMap(final Object node, final int shift, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
    final int len = nodeLength(node);
    Object[] result = new Object[len + 1];
    if (shift == 0) {
      for (int i = 0; i < len; i++) {
        result[i + 1] = dispatch.execute(function, nodeLookup(node, i));
      }
    } else {
      result[0] = ((Object[]) node)[0];
      for (int i = 0; i < len; i++) {
        result[i + 1] = nodeMap(nodeLookup(node, i), shift - BITS, function, dispatch);
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
        newRoot = newNonLeaf(root, wrap(leaf, 0, shift), shift);
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
        newRoot = newNonLeaf(root, wrap(leaf, 0, shift), shift);
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
    final int[] codePoints = source.codePoints().toArray();
    final ByteBuffer buffer = ByteBuffer.allocate(MAX_NODE_LENGTH * 4);
    return fromUtf8(new Utf8Source() {
      int cursor = 0;

      @Override
      int remaining() {
        return codePoints.length - cursor;
      }

      @Override
      byte[] next(final int offset, final int n) {
        int bytes = 0;
        for (int i = 0; i < n; i++) {
          int codePoint = codePoints[cursor++];
          int codePointLen = Util.utf8Length(codePoint);
          if (codePointLen == -1) {
            throw new AssertionError();
          }
          Util.utf8Encode(buffer, codePoint);
          bytes += codePointLen;
        }
        final byte[] result = new byte[offset + bytes];
        System.arraycopy(buffer.array(), 0, result, offset, bytes);
        buffer.position(0);
        return result;
      }
    });
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public static Seq catenate(final Seq left, final Seq right) {
    if (left.length() == 0) {
      return right;
    }
    if (right.length() == 0) {
      return left;
    }
    Object[] leftRoot = left.root;
    int leftShift = left.shift;
    if (nodeLength(left.prefix) != 0) {
      Object[] l = treeTryInsertFirst(leftRoot, left.prefix, leftShift);
      if (l != null) {
        leftRoot = l;
      } else {
        leftRoot = newNonLeaf(wrap(left.prefix, 0, leftShift), leftRoot, leftShift);
        leftShift += BITS;
      }
    }
    if (nodeLength(left.suffix) != 0) {
      Object[] l = treeTryInsertLast(leftRoot, left.suffix, leftShift);
      if (l != null) {
        leftRoot = l;
      } else {
        leftRoot = newNonLeaf(leftRoot, wrap(left.suffix, 0, leftShift), leftShift);
        leftShift += BITS;
      }
    }
    Object[] rightRoot = right.root;
    int rightShift = right.shift;
    if (nodeLength(right.prefix) != 0) {
      Object[] r = treeTryInsertFirst(rightRoot, right.prefix, rightShift);
      if (r != null) {
        rightRoot = r;
      } else {
        rightRoot = newNonLeaf(wrap(right.prefix, 0, rightShift), rightRoot, rightShift);
        rightShift += BITS;
      }
    }
    if (nodeLength(right.suffix) != 0) {
      Object[] r = treeTryInsertLast(rightRoot, right.suffix, rightShift);
      if (r != null) {
        rightRoot = r;
      } else {
        rightRoot = newNonLeaf(rightRoot, wrap(right.suffix, 0, rightShift), rightShift);
        rightShift += BITS;
      }
    }
    leftRoot = tiltRight(leftRoot, leftShift);
    rightRoot = tiltLeft(rightRoot, rightShift);
    final FirstAndRest prefixAndLeft = treeSeparateFirst(leftRoot, leftShift);
    final Object[] newPrefix = objectify(prefixAndLeft.first);
    leftRoot = prefixAndLeft.rest;
    final InitAndLast rightAndSuffix = treeSeparateLast(rightRoot, rightShift);
    rightRoot = rightAndSuffix.init;
    final Object[] newSuffix = objectify(rightAndSuffix.last);
    while (leftShift > BITS && nodeLength(leftRoot) == 1) {
      leftRoot = (Object[]) nodeFirst(leftRoot);
      leftShift -= BITS;
    }
    while (rightShift > BITS && nodeLength(rightRoot) == 1) {
      rightRoot = (Object[]) nodeFirst(rightRoot);
      rightShift -= BITS;
    }
    int shift = Math.max(leftShift, rightShift);
    final Object[] root;
    if (nodeLength(leftRoot) == 0) {
      root = rightRoot;
      shift = rightShift;
    } else if (nodeLength(rightRoot) == 0) {
      root = leftRoot;
      shift = leftShift;
    } else {
      leftRoot = (Object[]) wrap(leftRoot, leftShift, shift);
      rightRoot = (Object[]) wrap(rightRoot, rightShift, shift);
      root = newNonLeaf(leftRoot, rightRoot, shift);
      shift += BITS;
    }
    return new Seq(newPrefix, nodeLength(newPrefix), root, nodeSize(root, shift), newSuffix, nodeLength(newSuffix), shift);
  }

  static Object[] tiltLeft(final Object[] node, int shift) {
    if (shift == 6) {
      if (nodeLength(node) <= 1) {
        return node;
      } else if (nodeLength(nodeFirst(node)) >= MIN_NODE_LENGTH) {
        return node;
      } else {
        return redistributeLeft(node, 0);
      }
    } else {
      Object[] result = node;
      shift -= BITS;
      if (nodeLength(nodeFirst(node)) < MIN_NODE_LENGTH) {
        result = redistributeLeft(node, shift);
      }
      result = nonLeafReplaceFirst(result, tiltLeft((Object[]) nodeFirst(result), shift), shift);
      if (nodeLength(nodeFirst(node)) < MIN_NODE_LENGTH) {
        result = redistributeLeft(node, shift);
      }
      return result;
    }
  }

  static Object[] redistributeLeft(final Object[] parent, final int childShift) {
    final int parentLength = nodeLength(parent);
    int total = 0;
    Object[] result = null;
    int nodesToFill = 0;
    int nodesToCopy = 0;
    for (int idx = 0; idx < parentLength; idx++) {
      total += nodeLength(nodeLookup(parent, idx));
      if (total % MAX_NODE_LENGTH == 0) {
        nodesToFill = total / MAX_NODE_LENGTH;
        nodesToCopy = parentLength - idx - 1;
        result = new Object[nodesToFill + nodesToCopy + 1];
        for (int i = 0; i < nodesToFill; i++) {
          result[i + 1] = new Object[MAX_NODE_LENGTH + 1];
        }
        break;
      } else if (total % MIN_NODE_LENGTH == 0) {
        nodesToFill = total / MIN_NODE_LENGTH;
        nodesToCopy = parentLength - idx - 1;
        result = new Object[nodesToFill + nodesToCopy + 1];
        for (int i = 0; i < nodesToFill; i++) {
          result[i + 1] = new Object[MIN_NODE_LENGTH + 1];
        }
        break;
      }
    }
    if (result == null) {
      nodesToFill = (total / MAX_NODE_LENGTH) + 1;
      result = new Object[nodesToFill + 1];
      for (int i = 0; i < result.length - 1; i++) {
        result[i + 1] = new Object[MAX_NODE_LENGTH + 1];
      }
      result[result.length - 1] = new Object[(total % MAX_NODE_LENGTH) + 1];
    }
    int srcIdx = 0;
    int srcOffset = 0;
    for (int i = 0; i < nodesToFill; i++) {
      final Object[] dst = (Object[]) result[i + 1];
      int dstOffset = 0;
      while (srcIdx < parentLength) {
        Object[] src = objectify(nodeLookup(parent, srcIdx));
        final int srcRemaining = nodeLength(src) - srcOffset;
        final int dstRemaining = nodeLength(dst) - dstOffset;
        System.arraycopy(src, srcOffset + 1, dst, dstOffset + 1, Math.min(srcRemaining, dstRemaining));
        if (srcRemaining > dstRemaining) {
          srcOffset += dstRemaining;
          if (childShift != 0) {
            buildIndex(dst, childShift - BITS);
          }
          break;
        } else if (srcRemaining < dstRemaining) {
          dstOffset += srcRemaining;
          srcOffset = 0;
          srcIdx++;
        } else {
          srcOffset = 0;
          srcIdx++;
          if (childShift != 0) {
            buildIndex(dst, childShift - BITS);
          }
          break;
        }
      }
    }
    System.arraycopy(parent, srcIdx + 1, result, nodesToFill + 1, nodesToCopy);
    return buildIndex(result, childShift);
  }

  static Object[] tiltRight(final Object[] node, int shift) {
    if (shift == 6) {
      if (nodeLength(node) <= 1) {
        return node;
      } else if (nodeLength(nodeLast(node)) >= MIN_NODE_LENGTH) {
        return node;
      } else {
        return redistributeRight(node, 0);
      }
    } else {
      Object[] result = node;
      shift -= BITS;
      if (nodeLength(nodeLast(node)) < MIN_NODE_LENGTH) {
        result = redistributeRight(node, shift);
      }
      result = nonLeafReplaceLast(result, tiltRight((Object[]) nodeLast(result), shift), shift);
      if (nodeLength(nodeLast(node)) < MIN_NODE_LENGTH) {
        result = redistributeRight(node, shift);
      }
      return result;
    }
  }

  static Object[] redistributeRight(final Object[] parent, final int childShift) {
    final int parentLength = nodeLength(parent);
    int total = 0;
    Object[] result = null;
    int nodesToCopy = 0;
    int nodesToFill = 0;
    for (int idx = 0; idx < parentLength; idx++) {
      total += nodeLength(nodeLookup(parent, parentLength - idx - 1));
      if (total % MAX_NODE_LENGTH == 0) {
        nodesToFill = total / MAX_NODE_LENGTH;
        nodesToCopy = parentLength - idx - 1;
        result = new Object[nodesToFill + nodesToCopy + 1];
        for (int i = 0; i < nodesToFill; i++) {
          result[nodesToCopy + i + 1] = new Object[MAX_NODE_LENGTH + 1];
        }
        break;
      } else if (total % MIN_NODE_LENGTH == 0) {
        nodesToFill = total / MIN_NODE_LENGTH;
        nodesToCopy = parentLength - idx - 1;
        result = new Object[nodesToFill + nodesToCopy + 1];
        for (int i = 0; i < nodesToFill; i++) {
          result[nodesToCopy + i + 1] = new Object[MIN_NODE_LENGTH + 1];
        }
        break;
      }
    }
    if (result == null) {
      nodesToFill = (total / MAX_NODE_LENGTH) + 1;
      result = new Object[nodesToFill + 1];
      result[1] = new Object[(total % MAX_NODE_LENGTH) + 1];
      for (int i = 2; i < result.length; i++) {
        result[i] = new Object[MAX_NODE_LENGTH + 1];
      }
    }
    System.arraycopy(parent, 1, result, 1, nodesToCopy);
    int srcIdx = nodesToCopy;
    int srcOffset = 0;
    for (int i = 0; i < nodesToFill; i++) {
      final Object[] dst = (Object[]) result[nodesToCopy + i + 1];
      int dstOffset = 0;
      while (srcIdx < parentLength) {
        Object[] src = objectify(nodeLookup(parent, srcIdx));
        final int srcRemaining = nodeLength(src) - srcOffset;
        final int dstRemaining = nodeLength(dst) - dstOffset;
        System.arraycopy(src, srcOffset + 1, dst, dstOffset + 1, Math.min(srcRemaining, dstRemaining));
        if (srcRemaining > dstRemaining) {
          srcOffset += dstRemaining;
          if (childShift != 0) {
            buildIndex(dst, childShift - BITS);
          }
          break;
        } else if (srcRemaining < dstRemaining) {
          dstOffset += srcRemaining;
          srcOffset = 0;
          srcIdx++;
        } else {
          srcOffset = 0;
          srcIdx++;
          if (childShift != 0) {
            buildIndex(dst, childShift - BITS);
          }
          break;
        }
      }
    }
    return buildIndex(result, childShift);
  }

  static Object[] buildIndex(final Object[] nonLeaf, final int childShift) {
    final int len = nodeLength(nonLeaf);
    final long[] meta = new long[len];
    long base = 0;
    boolean isSpecial = false;
    for (int i = 0; i < len; i++) {
      final Object node = nodeLookup(nonLeaf, i);
      base += nodeSize(node, childShift);
      meta[i] = base;
      if (!isSpecial && nodeIsSpecial(node)) {
        isSpecial = true;
      }
    }
    if (isSpecial) {
      nonLeaf[0] = meta;
    }
    return nonLeaf;
  }
}
