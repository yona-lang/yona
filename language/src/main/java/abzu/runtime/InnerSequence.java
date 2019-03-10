package abzu.runtime;

abstract class InnerSequence {

  public abstract InnerSequence push(Object[] node);

  public abstract InnerSequence inject(Object[] node);

  public abstract Object[] first();

  public abstract Object[] last();

  public abstract InnerSequence removeFirst();

  public abstract InnerSequence removeLast();

  abstract int measure();

  abstract boolean empty();

  static final class Shallow extends InnerSequence {
    static final Shallow EMPTY = new Shallow();

    private final Object[] val;

    private Shallow(Object[] node) {
      val = node;
    }

    private Shallow() {
      val = null;
    }

    @Override
    public InnerSequence push(Object[] node) {
      return val == null ? new Shallow(node) : new Deep(node, val);
    }

    @Override
    public InnerSequence inject(Object[] node) {
      return val == null ? new Shallow(node) : new Deep(val, node);
    }

    @Override
    public Object[] first() {
      assert val != null;
      return val;
    }

    @Override
    public Object[] last() {
      assert val != null;
      return val;
    }

    @Override
    public InnerSequence removeFirst() {
      assert val != null;
      return EMPTY;
    }

    @Override
    public InnerSequence removeLast() {
      assert val != null;
      return EMPTY;
    }

    @Override
    int measure() {
      if (val == null) return 0;
      int result = 0;
      // TODO
      return result;
    }

    @Override
    boolean empty() {
      return val == null;
    }
  }

  static final class Deep extends InnerSequence {

    private Deep(Object[] first, Object[] second) {
      // TODO
    }

    @Override
    public InnerSequence push(Object[] node) {
      return null; // TODO
    }

    @Override
    public InnerSequence inject(Object[] node) {
      return null; // TODO
    }

    @Override
    public Object[] first() {
      return new Object[0]; // TODO
    }

    @Override
    public Object[] last() {
      return new Object[0]; // TODO
    }

    @Override
    public InnerSequence removeFirst() {
      return null; // TODO
    }

    @Override
    public InnerSequence removeLast() {
      return null; // TODO
    }

    @Override
    int measure() {
      return 0; // TODO
    }

    @Override
    boolean empty() {
      return false; // TODO
    }
  }

  /*
      @Override
    public OuterSequenceOLD push(Object value) {
      assert prefix.length <= 3;
      if (prefix.length == 3) return new Deep(new Object[] { value }, inner.push(prefix), suffix);
      final Object[] newPrefix = new Object[prefix.length + 1];
      newPrefix[0] = value;
      arraycopy(prefix, 0, newPrefix, 1, prefix.length);
      return new Deep(newPrefix, inner, suffix);
    }

    @Override
    public OuterSequenceOLD inject(Object value) {
      assert suffix.length <= 3;
      if (suffix.length == 3) return new Deep(prefix, inner.inject(suffix), new Object[] { value });
      final Object[] newSuffix = new Object[suffix.length + 1];
      arraycopy(suffix, 0, newSuffix, 0, suffix.length);
      newSuffix[suffix.length] = value;
      return new Deep(prefix, inner, newSuffix);
    }

    @Override
    public Object first() {
      return prefix[0];
    }

    @Override
    public Object last() {
      return suffix[suffix.length - 1];
    }

    @Override
    public OuterSequenceOLD removeFirst() {
      assert prefix.length <= 3;
      if (prefix.length > 1) {
        final Object[] newPrefix = new Object[prefix.length - 1];
        arraycopy(prefix, 1, newPrefix, 0, prefix.length - 1);
        return new Deep(newPrefix, inner, suffix);
      }
      if (!inner.empty()) return new Deep(inner.first(), inner.removeFirst(), suffix);
      if (suffix.length > 1) {
        final Object[] newPrefix = new Object[] { suffix[0] };
        final Object[] newSuffix = new Object[suffix.length - 1];
        arraycopy(suffix, 1, newSuffix, 0, suffix.length - 1);
        return new Deep(newPrefix, InnerSequence.Shallow.EMPTY, newSuffix);
      }
      return new Shallow(suffix[0]);
    }

    @Override
    public OuterSequenceOLD removeLast() {
      assert suffix.length <= 3;
      if (suffix.length > 1) {
        final Object[] newSuffix = new Object[suffix.length - 1];
        arraycopy(suffix, 0, newSuffix, 0, suffix.length - 1);
        return new Deep(prefix, inner, newSuffix);
      }
      if (!inner.empty()) return new Deep(prefix, inner.removeLast(), inner.last());
      if (prefix.length > 1) {
        final Object[] newPrefix = new Object[prefix.length - 1];
        arraycopy(prefix, 0, newPrefix, 0, prefix.length - 1);
        final Object[] newSuffix = new Object[] { prefix[prefix.length - 1] };
        return new Deep(newPrefix, InnerSequence.Shallow.EMPTY, newSuffix);
      }
      return new Shallow(prefix[0]);
    }
   */
}
