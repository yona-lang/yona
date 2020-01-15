package yatta.runtime.threading;

import java.util.concurrent.atomic.AtomicLongFieldUpdater;

final class AtomicCursor extends CursorWrite {
  static final AtomicLongFieldUpdater<AtomicCursor> VALUE_UPDATER = AtomicLongFieldUpdater.newUpdater(AtomicCursor.class, "value");

  private volatile long value = -1;

  @Override
  public long readVolatile() {
    return value;
  }

  @Override
  void writeOrdered(long value) {
    VALUE_UPDATER.lazySet(this, value);
  }

  boolean compareAndSwap(long expected, long updated) {
    return VALUE_UPDATER.compareAndSet(this, expected, updated);
  }

}
