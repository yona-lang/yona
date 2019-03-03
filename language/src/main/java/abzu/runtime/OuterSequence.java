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
    volatile int length = -1;

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
      final Object[] node = new Object[] { prefixOuter, prefixInner, new int[] { measure(prefixOuter), measure(prefixInner) } };
      return new Deep(o, null, innerSequence.push(node), suffixInner, suffixOuter);
    }

    @Override
    public OuterSequence inject(Object o) {
      if (suffixInner == null) return new Deep(prefixOuter, prefixInner, innerSequence, suffixOuter, o);
      final Object[] node = new Object[] { suffixInner, suffixOuter, new int[] { measure(suffixInner), measure(suffixOuter) } };
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
      if (length == -1) {
        int result = 0;
        result += measure(prefixOuter);
        if (prefixInner != null) result += measure(prefixInner);
        result += innerSequence.measure();
        if (suffixInner != null) result += measure(suffixInner);
        result += measure(suffixOuter);
        length = result;
      }
      return length;
    }

    @Override
    public boolean empty() {
      return false;
    }
  }
}
