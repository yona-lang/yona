package abzu.runtime;

import com.oracle.truffle.api.nodes.Node;

public abstract class OuterSequence {

  public abstract OuterSequence push(Object o);

  public abstract OuterSequence inject(Object o);

  public abstract Object first();

  public abstract Object last();

  public abstract OuterSequence removeFirst();

  public abstract OuterSequence removeLast();

  public abstract Object lookup(int idx, Node node);

  public abstract int length();

  public abstract boolean empty();

  public static OuterSequence sequence() {
    return Shallow.EMPTY;
  }

  private static int measure(Object o) {
    return o instanceof byte[] ? 0x7fffffff & readMeta((byte[]) o) : 1;
  }

  static int varIntLength(int value) {
    assert value >= 0;
    for (int i = 1; i < 5; i++) {
      if (((~0 << (7 * i)) & value) == 0) return i;
    }
    return 5;
  }

  static void varIntWrite(int value, byte[] destination, int offset) {
    assert value >= 0;
    while ((0xffffff80 & value) != 0) {
      destination[offset++] = (byte) ((0x7f & value) | 0x80);
      value >>>= 7;
    }
    destination[offset] = (byte) value;
  }

  static int varIntRead(byte[] source, int offset) {
    int result = 0;
    byte piece;
    for (int shift = 0; shift <= 28; shift += 7) {
      piece = source[offset++];
      result |= (0x7f & piece) << shift;
      if ((0x80 & piece) == 0) return result;
    }
    return -1;
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

  /*static boolean decodeType(int meta) {
    return ((meta & 0x80000000) >>> 31) == 1;
  }

  static int decodeLength(int meta) {
    return 0x7fffffff & meta;
  }

  static int encode(int type, int length) {
    return (type << 31) | length;
  }*/

  static int offsetOf(byte[] bytes, int idx) {
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

  static Object fromBytes(byte[] bytes, int idx) {
    final int meta = readMeta(bytes);
    if (meta > 0) return bytes[idx + 4];
    final int offset = offsetOf(bytes, idx);
    switch ((0xf0 & bytes[offset]) >>> 4) {
      case 0b0000:
      case 0b0001:
      case 0b0010:
      case 0b0011:
      case 0b0100:
      case 0b0101:
      case 0b0110:
      case 0b0111:
        return new Char(bytes[offset]);
      case 0b1000:
      case 0b1001:
      case 0b1010:
      case 0b1011:
        throw new AssertionError();
      case 0b1100:
      case 0b1101:
        return new Char(bytes[offset], bytes[offset + 1]);
      case 0b1110:
        return new Char(bytes[offset], bytes[offset + 1], bytes[offset + 2]);
      case 0b1111:
        return new Char(bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]);
      default:
        throw new AssertionError();
    }
  }

  static Object removeFirst(byte[] bytes) {
    final int meta = readMeta(bytes);
    switch (0x7fffffff & meta) {
      case 1: throw new AssertionError();
      case 2: return fromBytes(bytes, 1);
      default:
        final int offset = meta > 0 ? 5 : offsetOf(bytes, 1);
        final byte[] newBytes = new byte[bytes.length - offset + 4];
        writeMeta(newBytes, meta - 1);
        System.arraycopy(bytes, offset,  newBytes, 4, newBytes.length - 4);
        return newBytes;
    }
  }

  static Object removeLast(byte[] bytes) {
    final int meta = readMeta(bytes);
    final int len = 0x7fffffff & meta;
    switch (len) {
      case 1: throw new AssertionError();
      case 2: return fromBytes(bytes, 0);
      default:
        final int offset = meta > 0 ? bytes.length - 1 : offsetOf(bytes, len - 1);
        final byte[] newBytes = new byte[offset];
        writeMeta(newBytes, meta - 1);
        System.arraycopy(bytes, 4,  newBytes, 4, newBytes.length - 4);
        return newBytes;
    }
  }

  private static final class Shallow extends OuterSequence {
    static final Shallow EMPTY = new Shallow();

    final Object val;

    Shallow() {
      val = null;
    }

    Shallow(Object sole) {
      val = sole;
    }

    @Override
    public OuterSequence push(Object o) {
      return val == null ? new Shallow(o) : new Deep(o, val);
    }

    @Override
    public OuterSequence inject(Object o) {
      return val == null ? new Shallow(o) : new Deep(val, o);
    }

    @Override
    public Object first() {
      if (val instanceof byte[]) {
        final byte[] bytes = (byte[]) val;
        return fromBytes(bytes, 0);
      }
      return val;
    }

    @Override
    public Object last() {
      if (val instanceof byte[]) {
        final byte[] bytes = (byte[]) val;
        return fromBytes(bytes, (0x7fffffff & readMeta(bytes)) - 1);
      }
      return val;
    }

    @Override
    public OuterSequence removeFirst() {
      if (val instanceof byte[]) return new Shallow(removeFirst((byte[]) val));
      return EMPTY;
    }

    @Override
    public OuterSequence removeLast() {
      if (val instanceof byte[]) return new Shallow(removeLast((byte[]) val));
      return EMPTY;
    }

    @Override
    public Object lookup(int idx, Node node) {
      if (val == null) throw new BadArgException("Index out of bounds", node);
      if (val instanceof byte[]) {
        final byte[] bytes = (byte[]) val;
        final int len = 0x7fffffff & readMeta(bytes);
        if (idx < 0 || idx >= len) throw new BadArgException("Index out of bounds", node);
        return fromBytes(bytes, idx);
      }
      if (idx == 0) return val;
      throw new BadArgException("Index out of bounds", node);
    }

    @Override
    public int length() {
      return val == null ? 0 : measure(val);
    }

    @Override
    public boolean empty() {
      return val == null;
    }

    @Override
    public boolean equals(Object o) {
      if (!(o instanceof OuterSequence.Shallow)) return false;
      final OuterSequence.Shallow that = (OuterSequence.Shallow) o;
      if (this.val == null) return that.val == null;
      return this.val.equals(that.val);
    }
  }

  private static final class Deep extends OuterSequence {
    final Object prefixOuter;
    final Object prefixInner;
    final InnerSequence innerSequence;
    final Object suffixInner;
    final Object suffixOuter;
    int prefixLength = -1;
    int suffixLength = -1;

    Deep(Object first, Object second) {
      prefixOuter = first;
      prefixInner = null;
      innerSequence = InnerSequence.Shallow.EMPTY;
      suffixInner = null;
      suffixOuter = second;
    }

    Deep(Object prefixOuter, Object prefixInner, InnerSequence innerSequence, Object suffixInner, Object suffixOuter) {
      this.prefixOuter = prefixOuter;
      this.prefixInner = prefixInner;
      this.innerSequence = innerSequence;
      this.suffixInner = suffixInner;
      this.suffixOuter = suffixOuter;
    }

    @Override
    public OuterSequence push(Object o) {
      if (prefixInner == null) return new Deep(o, prefixOuter, innerSequence, suffixInner, suffixOuter);
      final int firstMeasure = measure(prefixOuter);
      final int secondMeasure = measure(prefixInner);
      final int firstMeasureLength = varIntLength(firstMeasure);
      final int secondMeasureLength = varIntLength(secondMeasure);
      final byte[] measures = new byte[firstMeasureLength + secondMeasureLength];
      varIntWrite(firstMeasure, measures, 0);
      varIntWrite(secondMeasure, measures, firstMeasureLength);
      final Object[] node = new Object[] { prefixOuter, prefixInner, measures };
      return new Deep(o, null, innerSequence.push(node), suffixInner, suffixOuter);
    }

    @Override
    public OuterSequence inject(Object o) {
      if (suffixInner == null) return new Deep(prefixOuter, prefixInner, innerSequence, suffixOuter, o);
      final int firstMeasure = measure(suffixInner);
      final int secondMeasure = measure(suffixOuter);
      final int firstMeasureLength = varIntLength(firstMeasure);
      final int secondMeasureLength = varIntLength(secondMeasure);
      final byte[] measures = new byte[firstMeasureLength + secondMeasureLength];
      varIntWrite(firstMeasure, measures, 0);
      varIntWrite(secondMeasure, measures, firstMeasureLength);
      final Object[] node = new Object[] { suffixInner, suffixOuter, measures };
      return new Deep(prefixOuter, prefixInner, innerSequence.inject(node), null, o);
    }

    @Override
    public Object first() {
      if (prefixOuter instanceof byte[]) {
        final byte[] bytes = (byte[]) prefixOuter;
        return fromBytes(bytes, 0);
      }
      return prefixOuter;
    }

    @Override
    public Object last() {
      if (suffixOuter instanceof byte[]) {
        final byte[] bytes = (byte[]) suffixOuter;
        return fromBytes(bytes, (0x7fffffff & readMeta(bytes)) - 1);
      }
      return suffixOuter;
    }

    @Override
    public OuterSequence removeFirst() {
      if (prefixOuter instanceof byte[]) return new Deep(removeFirst((byte[]) prefixOuter), prefixInner, innerSequence, suffixInner, suffixOuter);
      if (prefixInner != null) return new Deep(prefixInner, null, innerSequence, suffixInner, suffixOuter);
      if (!innerSequence.empty()) {
        final Object[] node = innerSequence.first();
        switch (node.length) {
          case 2: return new Deep(node[0], null, innerSequence.removeFirst(), suffixInner, suffixOuter);
          case 3: return new Deep(node[0], node[1], innerSequence.removeFirst(), suffixInner, suffixOuter);
          default: throw new AssertionError();
        }
      }
      if (suffixInner != null) return new Deep(suffixInner, suffixOuter);
      return new Shallow(suffixOuter);
    }

    @Override
    public OuterSequence removeLast() {
      if (suffixOuter instanceof byte[]) return new Deep(prefixOuter, prefixInner, innerSequence, suffixInner, removeLast((byte[]) suffixOuter));
      if (suffixInner != null) return new Deep(prefixOuter, prefixInner, innerSequence, null, suffixInner);
      if (!innerSequence.empty()) {
        final Object[] node = innerSequence.last();
        switch (node.length) {
          case 2: return new Deep(prefixOuter, prefixInner, innerSequence.removeLast(), null, node[0]);
          case 3: return new Deep(prefixOuter, prefixInner, innerSequence.removeLast(), node[0], node[1]);
          default: throw new AssertionError();
        }
      }
      if (prefixInner != null) return new Deep(prefixOuter, prefixInner);
      return new Shallow(prefixOuter);
    }

    @Override
    public Object lookup(int idx, Node node) {
      if (idx < 0) throw new BadArgException("Index out of bounds", node);
      int measure = measure(prefixOuter);
      if (idx < measure) return prefixOuter instanceof byte[] ? fromBytes((byte[]) prefixOuter, idx) : prefixOuter;
      idx -= measure;
      measure = measure(prefixInner);
      if (idx < measure) return prefixInner instanceof byte[] ? fromBytes((byte[]) prefixInner, idx) : prefixInner;
      idx -= measure;
      measure = innerSequence.measure();
      if (idx < measure) {
        final InnerSequence.Split split = new InnerSequence.Split(false, false);
        idx = innerSequence.splitAt(idx, split);
        Object o;
        for (int i = 0; i < split.point.length - 1; i++) {
          o = split.point[i];
          measure = measure(o);
          if (idx < measure) return o instanceof byte[] ? fromBytes((byte[]) o, idx) : o;
          idx -= measure;
        }
        throw new AssertionError();
      }
      idx -= measure;
      measure = measure(suffixInner);
      if (idx < measure) return suffixInner instanceof byte[] ? fromBytes((byte[]) suffixInner, idx) : suffixInner;
      idx -= measure;
      measure = measure(suffixOuter);
      if (idx < measure) return suffixOuter instanceof byte[] ? fromBytes((byte[]) suffixOuter, idx) : suffixOuter;
      throw new BadArgException("Index out of bounds", node);
    }

    @Override
    public int length() {
      return prefixLength() + innerSequence.measure() + suffixLength();
    }

    private int prefixLength() {
      if (prefixLength == -1) {
        int result = 0;
        result += measure(prefixOuter);
        if (prefixInner != null) result += measure(prefixInner);
        prefixLength = result;
      }
      return prefixLength;
    }

    private int suffixLength() {
      if (suffixLength == -1) {
        int result = 0;
        if (suffixInner != null) result += measure(suffixInner);
        result += measure(suffixOuter);
        suffixLength = result;
      }
      return suffixLength;
    }

    @Override
    public boolean empty() {
      return false;
    }
  }
}
