package yatta.runtime.threading;

import java.util.concurrent.atomic.AtomicLong;

final class Consumer {
  private final AtomicLong cursor;
  private final AtomicLong sharedCursor;
  private final RingBuffer<?> buffer;

  Consumer(final AtomicLong sharedCursor, final AtomicLong cursor, final RingBuffer<?> buffer) {
    this.sharedCursor = sharedCursor;
    this.cursor = cursor;
    this.buffer = buffer;
  }

  boolean consume(final Callback callback) {
    long current;
    long next;
    long limit;
    do {
      current = sharedCursor.get();
      cursor.lazySet(current);
      next = current + 1;
      limit = buffer.lastReleased(next);
      if (next > limit) {
        return false;
      }
      callback.prepare(next);
    } while (!sharedCursor.compareAndSet(current, next));
    callback.advance();
    return true;
  }


  static abstract class Callback {
    abstract void prepare(long token);

    abstract void advance();
  }
}
