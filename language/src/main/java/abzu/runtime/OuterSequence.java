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

  private static int measure(Object o) {
    return 1;
  }

  static int varIntLength(int value) {
    assert value >= 0;
    int result = 1;
    while (result < 5) {
      if (((~0 << (7 * result)) & value) == 0) return result;
      result++;
    }
    return 5;
  }

  static void varIntWrite(int value, byte[] destination, int offset) {
    assert value >= 0;
    while ((~0x7f & value) != 0) {
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
      assert val != null;
      return val;
    }

    @Override
    public Object last() {
      assert val != null;
      return val;
    }

    @Override
    public OuterSequence removeFirst() {
      assert val != null;
      return EMPTY;
    }

    @Override
    public OuterSequence removeLast() {
      assert val != null;
      return EMPTY;
    }

    @Override
    public Object lookup(int idx, Node node) {
      if (val == null || idx != 0) throw new BadArgException("Index out of bounds", node);
      return val;
    }

    @Override
    public int length() {
      return val == null ? 0 : measure(val);
    }

    @Override
    public boolean empty() {
      return val == null;
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
      innerSequence = InnerSequence.empty();
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
      return prefixOuter;
    }

    @Override
    public Object last() {
      return suffixOuter;
    }

    @Override
    public OuterSequence removeFirst() {
      if (prefixInner != null) return new Deep(prefixInner, null, innerSequence, suffixInner, suffixOuter);
      if (!innerSequence.isEmpty()) {
        final Object[] node = innerSequence.first();
        switch (node.length) {
          case 2: return new Deep(node[0], null, innerSequence.removeFirst(), suffixInner, suffixOuter);
          case 3: return new Deep(node[0], node[1], innerSequence.removeFirst(), suffixInner, suffixOuter);
          default: {
            assert false;
            return null;
          }
        }
      }
      if (suffixInner != null) return new Deep(suffixInner, suffixOuter);
      return new Shallow(suffixOuter);
    }

    @Override
    public OuterSequence removeLast() {
      if (suffixInner != null) return new Deep(prefixOuter, prefixInner, innerSequence, null, suffixInner);
      if (!innerSequence.isEmpty()) {
        final Object[] node = innerSequence.last();
        switch (node.length) {
          case 2: return new Deep(prefixOuter, prefixInner, innerSequence.removeLast(), null, node[0]);
          case 3: return new Deep(prefixOuter, prefixInner, innerSequence.removeLast(), node[0], node[1]);
          default: {
            assert false;
            return null;
          }
        }
      }
      if (prefixInner != null) return new Deep(prefixOuter, prefixInner);
      return new Shallow(prefixOuter);
    }

    @Override
    public Object lookup(int idx, Node node) {
      if (idx < 0) throw new BadArgException("Index out of bounds", node);
      // TODO
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
