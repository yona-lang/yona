package yatta.runtime.threading;

final class Consumer {
  private final AtomicCursor cursor;
  private final AtomicCursor sharedCursor;
  private final RingBuffer<?> buffer;

  Consumer(final AtomicCursor sharedCursor, final AtomicCursor cursor, final RingBuffer<?> buffer) {
    this.sharedCursor = sharedCursor;
    this.cursor = cursor;
    this.buffer = buffer;
  }

  boolean consume(final Callback callback) {
    long current;
    long next;
    long available;
    do {
      current = sharedCursor.readVolatile();
      cursor.writeOrdered(current);
      next = current + 1;
      available = buffer.lastPublished(next, buffer.lastClaimed());
      if (next > available) {
        return false;
      }
      callback.ready(next);
    } while (!sharedCursor.compareAndSwap(current, next));
    callback.released();
    return true;
  }


  static abstract class Callback {
    abstract void ready(long token);

    abstract void released();
  }
}
