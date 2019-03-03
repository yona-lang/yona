package abzu.runtime;

abstract class InnerSequence {

  public abstract InnerSequence push(Object[] node);

  public abstract InnerSequence inject(Object[] node);

  public abstract Object[] first();

  public abstract Object[] last();

  public abstract InnerSequence removeFirst();

  public abstract InnerSequence removeLast();

  abstract int measure();

  abstract boolean isEmpty();

  public static InnerSequence empty() {
    return null; // TODO
  }
}
