package yatta.runtime.threading;

import java.util.Arrays;
import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import java.util.function.Supplier;

final class RingBuffer<E> {
  @SuppressWarnings("rawtypes")
  private static final AtomicReferenceFieldUpdater<RingBuffer, AtomicCursor[]> GATING_UPDATER = AtomicReferenceFieldUpdater.newUpdater(RingBuffer.class, AtomicCursor[].class, "gating");

  private final AtomicCursor cursor = new AtomicCursor();
  private final AtomicCursor cachedGating = new AtomicCursor();
  private final E[] elements;
  private final int idxMask;
  private final int idxShift;
  private final AtomicIntegerArray availability;
  private volatile AtomicCursor[] gating = new AtomicCursor[]{};

  @SuppressWarnings("unchecked")
  RingBuffer(final int size, final Supplier<? extends E> constructor) {
    if (Integer.bitCount(size) != 1) {
      throw new AssertionError();
    }
    elements = (E[]) new Object[size];
    for (int i = 0; i < size; i++) {
      elements[i] = constructor.get();
    }
    idxMask = size - 1;
    idxShift = 31 - Integer.numberOfLeadingZeros(size);
    availability = new AtomicIntegerArray(size);
    for (int i = 0; i < availability.length(); i++) {
      availability.set(i, -1);
    }
  }

  long tryClaim(final int n) {
    long current;
    long next;
    do {
      current = cursor.readVolatile();
      next = current + n;
      if (!hasCapacity(n, current)) {
        return -1L;
      }
    } while (!cursor.compareAndSwap(current, next));
    return next;
  }

  private boolean hasCapacity(final int n, final long current) {
    final long wrapsAt = (current + n) - elements.length;
    final long cachedGatingValue = cachedGating.readVolatile();
    if (current < cachedGatingValue || cachedGatingValue < wrapsAt) {
      long min = current;
      for (AtomicCursor cursor : gating) {
        min = Math.min(min, cursor.readVolatile());
      }
      cachedGating.writeOrdered(min);
      return wrapsAt <= min;
    }
    return true;
  }

  E read(final long token) {
    return elements[(int)(token & idxMask)];
  }

  void release(final long from, final long to) {
    for (long i = from; i <= to; i++) {
      availability.lazySet(((int) i) & idxMask, (int) (i >>> idxShift));
    }
  }

  Consumer[] subscribe(final int n) {
    final AtomicCursor sharedCursor = new AtomicCursor();
    final AtomicCursor[] consumerCursors = new AtomicCursor[n];
    for (int i = 0; i < n; i++) {
      consumerCursors[i] = new AtomicCursor();
    }
    final Consumer[] result = new Consumer[n];
    for (int i = 0; i < n; i++) {
      result[i] = new Consumer(sharedCursor, consumerCursors[i], this);
    }
    invite(consumerCursors);
    return result;
  }

  private void invite(final AtomicCursor[] cursors) {
    long cursorValue;
    AtomicCursor[] updated;
    AtomicCursor[] current;
    do {
      current = GATING_UPDATER.get(this);
      updated = Arrays.copyOf(current, current.length + cursors.length);
      cursorValue = cursor.readVolatile();
      int index = current.length;
      for (AtomicCursor c : cursors) {
        c.writeOrdered(cursorValue);
        updated[index++] = c;
      }
    } while (!GATING_UPDATER.compareAndSet(this, current, updated));
    cursorValue = cursor.readVolatile();
    for (AtomicCursor c : cursors) {
      c.writeOrdered(cursorValue);
    }
  }

  long lastPublished(final long from, final long to) {
    for (long i = from; i <= to; i++) {
      if (availability.get(((int) i) & idxMask) != (int) (i >>> idxShift)) {
        return i - 1;
      }
    }
    return to;
  }

  long lastClaimed() {
    return cursor.readVolatile();
  }
}
