package yatta.runtime.threading;

import java.util.concurrent.atomic.AtomicIntegerArray;

import static java.lang.Integer.numberOfLeadingZeros;
import static java.lang.Math.min;

final class RingBuffer {
  final Task[] entries;
  final int mask;
  final AtomicCursor cursor = new AtomicCursor();
  final DynamicMembershipCursors gate = new DynamicMembershipCursors();
  final AtomicCursor cachedGating = new AtomicCursor();
  final AtomicIntegerArray availability;
  final int availabilityShift;

  RingBuffer(final int size) {
    if (Integer.bitCount(size) != 1) {
      throw new IllegalArgumentException("Size must be power of two.");
    }
    entries = new Task[size];
    for (int i = 0; i < size; i++) {
      entries[i] = new Task();
    }
    mask = size - 1;
    availability = new AtomicIntegerArray(size);
    for (int i = 0; i < size; i++) availability.lazySet(i, -1);
    availabilityShift = 31 - numberOfLeadingZeros(size);
  }

  long tryClaim(int n) {
    long cursorValue;
    long result;
    do {
      cursorValue = cursor.readVolatile();
      result = cursorValue + n;
      final long wrapsAt = cursorValue + n - entries.length;
      final long cachedGatingValue = cachedGating.readVolatile();
      if (cursorValue < cachedGatingValue || cachedGatingValue < wrapsAt) {
        final long min = min(cursorValue, gate.readVolatile());
        cachedGating.writeOrdered(min);
        if (min < wrapsAt) return -1L;
      }
    } while (!cursor.compareAndSwap(cursorValue, result));
    return result;
  }

  Task slotFor(final long token) {
    return entries[(int)(token & mask)];
  }

  void publish(long from, long to) {
    for (long i = from; i <= to; i++) {
      availability.lazySet(((int) i) & mask, (int) (i >>> availabilityShift));
    }
  }

  ParallelConsumer[] subscribe(int n) {
    ParallelConsumer[] result = ParallelConsumer.create(this, n);
    gate.invite(cursor, result[0].groupCursor);
    return result;
  }

  void unsubscribe(ParallelConsumer consumer) {
    gate.expel(consumer.groupCursor);
  }

  long lastPublished(long from, long to) {
    for (long i = from; i <= to; i++) {
      if (availability.get(((int) i) & mask) != (int) (i >>> availabilityShift)) {
        return i - 1;
      }
    }
    return to;
  }

}
