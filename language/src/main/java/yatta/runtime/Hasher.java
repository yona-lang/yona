package yatta.runtime;

public abstract class Hasher {
  public abstract long hash(long seed, Object value);
}
