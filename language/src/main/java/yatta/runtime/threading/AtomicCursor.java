package yatta.runtime.threading;

import java.util.concurrent.atomic.AtomicLongFieldUpdater;

final class AtomicCursor extends CursorWrite {
  private static final AtomicLongFieldUpdater<AtomicCursor> VALUE_UPDATER = AtomicLongFieldUpdater.newUpdater(AtomicCursor.class, "value");

  private volatile long value = -1;

  boolean compareAndSwap(final long expect, final long update) {
    return VALUE_UPDATER.compareAndSet(this, expect, update);
  }

  @Override
  void writeOrdered(final long value) {
    VALUE_UPDATER.lazySet(this, value);
  }

  @Override
  long readVolatile() {
    return value;
  }
}
