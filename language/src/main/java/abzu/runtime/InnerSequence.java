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
}
