package abzu.runtime;

import com.oracle.truffle.api.nodes.Node;

import static abzu.runtime.Util.*;
import static java.lang.System.arraycopy;

public final class OuterSequence {
  public static final OuterSequence EMPTY = new OuterSequence(null, null, InnerSequence.Shallow.EMPTY, null, null);

  private final Object prefixOuter;
  private final Object prefixInner;
  private final InnerSequence innerSequence;
  private final Object suffixInner;
  private final Object suffixOuter;

  private int prefixLength = -1;
  private int suffixLength = -1;

  private int prefixLength() {
    if (prefixLength == -1) {
      int result = 0;
      if (prefixOuter != null) result += prefixOuter instanceof byte[] ? 0x7fffffff & readMeta((byte[]) prefixOuter) : 1;
      if (prefixInner != null) result += prefixInner instanceof byte[] ? 0x7fffffff & readMeta((byte[]) prefixInner) : 1;
      prefixLength = result;
    }
    return prefixLength;
  }

  private int suffixLength() {
    if (suffixLength == -1) {
      int result = 0;
      if (suffixInner != null) result += suffixInner instanceof byte[] ? 0x7fffffff & readMeta((byte[]) suffixInner) : 1;
      if (suffixOuter != null) result += suffixOuter instanceof byte[] ? 0x7fffffff & readMeta((byte[]) suffixOuter) : 1;
      suffixLength = result;
    }
    return suffixLength;
  }

  private OuterSequence(Object prefixOuter, Object prefixInner, InnerSequence innerSequence, Object suffixInner, Object suffixOuter) {
    this.prefixOuter = prefixOuter;
    this.prefixInner = prefixInner;
    this.innerSequence = innerSequence;
    this.suffixInner = suffixInner;
    this.suffixOuter = suffixOuter;
  }

  public OuterSequence push(Object o) {
    if (prefixInner == null) return new OuterSequence(null, o, innerSequence, suffixInner, suffixOuter);
    if (prefixOuter == null) return new OuterSequence(o, prefixInner, innerSequence, suffixInner, suffixOuter);
    final int fstMeasure = o instanceof byte[] ?  0x7fffffff & readMeta((byte[]) o) : 1;
    final int sndMeasure = prefixOuter instanceof byte[] ?  0x7fffffff & readMeta((byte[]) prefixOuter) : 1;
    final int trdMeasure = prefixInner instanceof byte[] ?  0x7fffffff & readMeta((byte[]) prefixInner) : 1;
    final int fstMeasureLen = varIntLen(fstMeasure);
    final int sndMeasureLen = varIntLen(sndMeasure);
    final int trdMeasureLen = varIntLen(trdMeasure);
    final byte[] measures = new byte[fstMeasureLen + sndMeasureLen + trdMeasureLen];
    int offset = 0;
    varIntWrite(fstMeasure, measures, offset);
    offset += fstMeasureLen;
    varIntWrite(sndMeasure, measures, offset);
    offset += sndMeasureLen;
    varIntWrite(trdMeasure, measures, offset);
    return new OuterSequence(null, null, innerSequence.push(new Object[]{ measures, o, prefixOuter, prefixInner }, fstMeasure + sndMeasure + trdMeasure), suffixInner, suffixOuter);
  }

  public OuterSequence inject(Object o) {
    if (suffixInner == null) return new OuterSequence(prefixOuter, prefixInner, innerSequence, o, null);
    if (suffixOuter == null) return new OuterSequence(prefixOuter, prefixInner, innerSequence, prefixInner, o);
    final int fstMeasure = suffixInner instanceof byte[] ?  0x7fffffff & readMeta((byte[]) suffixInner) : 1;
    final int sndMeasure = suffixOuter instanceof byte[] ?  0x7fffffff & readMeta((byte[]) suffixOuter) : 1;
    final int trdMeasure = o instanceof byte[] ?  0x7fffffff & readMeta((byte[]) o) : 1;
    final int fstMeasureLen = varIntLen(fstMeasure);
    final int sndMeasureLen = varIntLen(sndMeasure);
    final int trdMeasureLen = varIntLen(trdMeasure);
    final byte[] measures = new byte[fstMeasureLen + sndMeasureLen + trdMeasureLen];
    int offset = 0;
    varIntWrite(fstMeasure, measures, offset);
    offset += fstMeasureLen;
    varIntWrite(sndMeasure, measures, offset);
    offset += sndMeasureLen;
    varIntWrite(trdMeasure, measures, offset);
    return new OuterSequence(prefixOuter, prefixInner, innerSequence.inject(new Object[] { measures, suffixInner, suffixOuter, o }, fstMeasure + sndMeasure + trdMeasure), null, null);
  }

  public Object first() {
    Object result;
    if (prefixOuter != null) result = prefixOuter;
    else if (prefixInner != null) result = prefixInner;
    else if (!innerSequence.empty()) {
      final Object[] node = innerSequence.first();
      result = node[1];
    }
    else if (suffixInner != null) result = suffixInner;
    else if (suffixOuter != null) result = suffixOuter;
    else throw new AssertionError();
    return first(result);
  }

  private static Object first(Object o) {
    if (o instanceof byte[]) {
      final byte[] bytes = (byte[]) o;
      final int meta = readMeta(bytes);
      if (meta > 0) return bytes[4];
      else return codePointAt(bytes, 4);
    }
    return o;
  }

  public Object last() {
    Object result;
    if (suffixOuter != null) result = suffixOuter;
    else if (suffixInner != null) result = suffixInner;
    else if (!innerSequence.empty()) {
      final Object[] node = innerSequence.last();
      result = node[node.length - 1];
    }
    else if (prefixInner != null) result = prefixInner;
    else if (prefixOuter != null) result = prefixOuter;
    else throw new AssertionError();
    return last(result);
  }

  private static Object last(Object o) {
    if (o instanceof byte[]) {
      final byte[] bytes = (byte[]) o;
      final int meta = readMeta(bytes);
      int offset = bytes.length - 1;
      if (meta > 0) return bytes[offset];
      while (((0x80 & bytes[offset]) >>> 6) == 0x2) offset--;
      return codePointAt(bytes, offset);
    }
    return o;
  }

  public OuterSequence pop() {
    if (prefixOuter != null) return new OuterSequence(removeFirst(prefixOuter), prefixInner, innerSequence, suffixInner, suffixOuter);
    if (prefixInner != null) return new OuterSequence(null, removeFirst(prefixInner), innerSequence, suffixInner, suffixOuter);
    if (!innerSequence.empty()) {
      final Object[] node = innerSequence.first();
      InnerSequence newInnerSequence = innerSequence.pop();
      switch (node.length) {
        case 2: return new OuterSequence(null, removeFirst(node[1]), newInnerSequence, suffixInner, suffixOuter);
        case 3: return new OuterSequence(removeFirst(node[1]), node[2], newInnerSequence, suffixInner, suffixOuter);
        case 4:
          final Object remainder = removeFirst(node[1]);
          if (remainder == null) return new OuterSequence(node[2], node[3], newInnerSequence, suffixInner, suffixOuter);
          final Object n = node[3];
          final int measure = n instanceof byte[] ?  0x7fffffff & readMeta((byte[]) n) : 1;
          final byte[] measures = new byte[varIntLen(measure)];
          varIntWrite(measure, measures, 0);
          newInnerSequence = newInnerSequence.push(new Object[] { measures, n }, measure);
          return new OuterSequence(remainder, node[2], newInnerSequence, suffixInner, suffixOuter);
        default: throw new AssertionError();
      }
    }
    if (suffixInner != null) return new OuterSequence(null, removeFirst(suffixInner), InnerSequence.Shallow.EMPTY, suffixOuter, null);
    throw new AssertionError();
  }

  private static Object removeFirst(Object o) {
    if (o instanceof byte[]) {
      final byte[] bytes = (byte[]) o;
      final int meta = readMeta(bytes);
      switch (0x7fffffff & meta) {
        case 1: return null;
        case 2:
          if (meta > 0) return bytes[5];
          else return codePointAt(bytes, 4 + codePointLen(codePointAt(bytes, 4)));
        default:
          final int offset = meta > 0 ? 1 : 4 + codePointLen(codePointAt(bytes, 4));
          final byte[] result = new byte[bytes.length - offset];
          writeMeta(result, meta - 1);
          arraycopy(bytes, 4 + offset, result, 4, result.length - 4);
          return result;
      }
    }
    return null;
  }

  public OuterSequence eject() {
    if (suffixOuter != null) return new OuterSequence(prefixOuter, prefixInner, innerSequence, suffixInner, null);
    if (suffixInner != null) return new OuterSequence(prefixOuter, prefixInner, innerSequence, null, null);
    if (!innerSequence.empty()) {
      final Object[] node = innerSequence.last();
      InnerSequence newInnerSequence = innerSequence.eject();
      switch (node.length) {
        case 2: return new OuterSequence(prefixOuter, prefixInner, newInnerSequence, removeLast(node[1]), null);
        case 3: return new OuterSequence(prefixOuter, prefixInner, newInnerSequence, node[1], removeLast(node[2]));
        case 4:
          final Object remainder = removeLast(node[3]);
          if (remainder == null) return new OuterSequence(prefixOuter, prefixInner, newInnerSequence, node[1], node[2]);
          final Object n = node[1];
          final int measure = n instanceof byte[] ?  0x7fffffff & readMeta((byte[]) n) : 1;
          final byte[] measures = new byte[varIntLen(measure)];
          varIntWrite(measure, measures, 0);
          newInnerSequence = newInnerSequence.inject(new Object[] { measures, n }, measure);
          return new OuterSequence(prefixOuter, prefixInner, newInnerSequence, node[2], remainder);
      }
    }
    if (prefixInner != null) return new OuterSequence(null, prefixOuter, InnerSequence.Shallow.EMPTY, removeLast(prefixInner), null);
    throw new AssertionError();
  }

  private static Object removeLast(Object o) {
    if (o instanceof byte[]) {
      final byte[] bytes = (byte[]) o;
      final int meta = readMeta(bytes);
      switch (0x7fffffff & meta) {
        case 1: return null;
        case 2:
          if (meta > 0) return bytes[4];
          else return codePointAt(bytes, offsetUtf8(bytes, 0));
        default:
          int offset = bytes.length - 1;
          if (meta < 0) while (((0x80 & bytes[offset]) >>> 6) == 0x2) offset--;
          final byte[] result = new byte[offset];
          writeMeta(result, meta - 1);
          arraycopy(bytes, 4, result, 4, result.length - 4);
          return result;
      }
    }
    return null;
  }

  public Object lookup(final int idx, Node node) {
    int i = idx;
    if (i < 0) throw new BadArgException("Index out of bounds: " + idx, node);
    int measure;
    if (prefixOuter != null) {
      if (prefixOuter instanceof byte[]) {
        final byte[] bytes = (byte[]) prefixOuter;
        final int meta = readMeta(bytes);
        measure = 0x7fffffff & meta;
        if (i < measure) {
          if (meta > 0) return bytes[4 + i];
          else return codePointAt(bytes, offsetUtf8(bytes, i));
        }
        i -= measure;
      } else {
        if (i < 1) return prefixOuter;
        i--;
      }
    }
    if (prefixInner != null) {
      if (prefixInner instanceof byte[]) {
        final byte[] bytes = (byte[]) prefixInner;
        final int meta = readMeta(bytes);
        measure = 0x7fffffff & meta;
        if (i < measure) {
          if (meta > 0) return bytes[4 + i];
          else return codePointAt(bytes, offsetUtf8(bytes, i));
        }
        i -= measure;
      } else {
        if (i < 1) return prefixInner;
        i--;
      }
    }
    measure = innerSequence.measure();
    if (i < measure) {
      final InnerSequence.Split split = new InnerSequence.Split(false, false);
      i = innerSequence.split(i, split);
      Object cursor;
      for (int j = 1; j < split.point.length; j++) {
        cursor = split.point[j];
        if (cursor instanceof byte[]) {
          final byte[] bytes = (byte[]) cursor;
          final int meta = readMeta(bytes);
          if (i < measure) {
            if (meta > 0) return bytes[4 + i];
            else return codePointAt(bytes, offsetUtf8(bytes, i));
          }
          i -= measure;
        } else {
          if (i < 1) return cursor;
          i--;
        }
      }
      throw new AssertionError();
    }
    i -= measure;
    if (suffixInner != null) {
      if (suffixInner instanceof byte[]) {
        final byte[] bytes = (byte[]) suffixInner;
        final int meta = readMeta(bytes);
        measure = 0x7fffffff & meta;
        if (i < measure) {
          if (meta > 0) return bytes[4 + i];
          else return codePointAt(bytes, offsetUtf8(bytes, i));
        }
        i -= measure;
      } else {
        if (i < 1) return suffixInner;
        i--;
      }
    }
    if (suffixOuter != null) {
      if (suffixOuter instanceof byte[]) {
        final byte[] bytes = (byte[]) suffixOuter;
        final int meta = readMeta(bytes);
        measure = 0x7fffffff & meta;
        if (i < measure) {
          if (meta > 0) return bytes[4 + i];
          else return codePointAt(bytes, offsetUtf8(bytes, i));
        }
        i -= measure;
      } else {
        if (i < 1) return suffixOuter;
      }
    }
    throw new BadArgException("Index out of bounds: " + idx, node);
  }

  static int offsetUtf8(byte[] bytes, int idx) {
    final int len = 0x7fffffff & readMeta(bytes);
    if (idx < len/2) {
      int offset = 4;
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

  public int length() {
    return prefixLength() + innerSequence.measure() + suffixLength();
  }

  public boolean empty() {
    return prefixOuter == null && prefixInner == null && innerSequence.empty() && suffixInner == null && suffixOuter == null;
  }

  public static OuterSequence catenate(OuterSequence left, OuterSequence right) {
    final InnerSequence leftInner;
    if (left.suffixOuter != null) {
      final int fstMeasure = left.suffixInner instanceof byte[] ?  0x7fffffff & readMeta((byte[]) left.suffixInner) : 1;
      final int sndMeasure = left.suffixOuter instanceof byte[] ?  0x7fffffff & readMeta((byte[]) left.suffixOuter) : 1;
      final int fstMeasureLen = varIntLen(fstMeasure);
      final int sndMeasureLen = varIntLen(sndMeasure);
      final byte[] measures = new byte[fstMeasureLen + sndMeasureLen];
      int offset = 0;
      varIntWrite(fstMeasure, measures, offset);
      offset += fstMeasureLen;
      varIntWrite(sndMeasure, measures, offset);
      leftInner = left.innerSequence.inject(new Object[]{ measures, left.suffixInner, left.suffixOuter }, fstMeasure + sndMeasure);
    } else if (left.suffixInner != null) {
      final int measure = left.suffixInner instanceof byte[] ?  0x7fffffff & readMeta((byte[]) left.suffixInner) : 1;
      final byte[] measures = new byte[varIntLen(measure)];
      varIntWrite(measure, measures, 0);
      leftInner = left.innerSequence.inject(new Object[]{ measures, left.suffixInner }, measure);
    } else leftInner = left.innerSequence;
    final InnerSequence rightInner;
    if (right.prefixOuter != null) {
      final int fstMeasure = right.prefixOuter instanceof byte[] ?  0x7fffffff & readMeta((byte[]) right.prefixOuter) : 1;
      final int sndMeasure = right.prefixInner instanceof byte[] ?  0x7fffffff & readMeta((byte[]) right.prefixInner) : 1;
      final int fstMeasureLen = varIntLen(fstMeasure);
      final int sndMeasureLen = varIntLen(sndMeasure);
      final byte[] measures = new byte[fstMeasureLen + sndMeasureLen];
      int offset = 0;
      varIntWrite(fstMeasure, measures, offset);
      offset += fstMeasureLen;
      varIntWrite(sndMeasure, measures, offset);
      rightInner = right.innerSequence.push(new Object[]{ measures, right.prefixOuter, right.prefixInner }, fstMeasure + sndMeasure);
    } else if (right.prefixInner != null) {
      final int measure = right.prefixInner instanceof byte[] ?  0x7fffffff & readMeta((byte[]) right.prefixInner) : 1;
      final byte[] measures = new byte[varIntLen(measure)];
      varIntWrite(measure, measures, 0);
      rightInner = right.innerSequence.push(new Object[]{ measures, right.prefixInner }, measure);
    } else rightInner = right.innerSequence;
    return new OuterSequence(left.prefixOuter, left.prefixInner, InnerSequence.catenate(leftInner, rightInner), right.suffixInner, right.suffixOuter);
  }

  static int readMeta(byte[] source) {
    int result = 0;
    for (int i = 0; i < 4; i++) {
      result |= (0xff & source[i]) << (8 * i);
    }
    return result;
  }

  static void writeMeta(byte[] destination, int value) {
    destination[0] = (byte) value;
    destination[1] = (byte)(value >> 8);
    destination[2] = (byte)(value >> 16);
    destination[3] = (byte)(value >> 24);
  }
}
