package yatta.runtime.threading;

import java.util.Arrays;
import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import java.util.function.Supplier;

final class RingBuffer<E> {
  @SuppressWarnings("rawtypes")
  private static final AtomicReferenceFieldUpdater<RingBuffer, AtomicLong[]> GATING_UPDATER = AtomicReferenceFieldUpdater.newUpdater(RingBuffer.class, AtomicLong[].class, "gating");

  private final AtomicLong cursor = new AtomicLong(-1L);
  private final AtomicLong cachedGating = new AtomicLong(-1L);
  private final E[] elements;
  private final int idxMask;
  private final int idxShift;
  private final AtomicIntegerArray availability;
  private volatile AtomicLong[] gating = new AtomicLong[]{};

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
      current = cursor.get();
      next = current + n;
      if (!hasCapacity(n, current)) {
        return -1L;
      }
    } while (!cursor.compareAndSet(current, next));
    return next;
  }

  private boolean hasCapacity(final int n, final long current) {
    final long wrapsAt = (current + n) - elements.length;
    final long cachedGatingValue = cachedGating.get();
    if (current < cachedGatingValue || cachedGatingValue < wrapsAt) {
      long min = current;
      for (AtomicLong cursor : gating) {
        min = Math.min(min, cursor.get());
      }
      cachedGating.lazySet(min);
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
    final AtomicLong sharedCursor = new AtomicLong(-1L);
    final AtomicLong[] consumerCursors = new AtomicLong[n];
    for (int i = 0; i < n; i++) {
      consumerCursors[i] = new AtomicLong(-1L);
    }
    final Consumer[] result = new Consumer[n];
    for (int i = 0; i < n; i++) {
      result[i] = new Consumer(sharedCursor, consumerCursors[i], this);
    }
    invite(consumerCursors);
    return result;
  }

  private void invite(final AtomicLong[] cursors) {
    long cursorValue;
    AtomicLong[] updated;
    AtomicLong[] current;
    do {
      current = GATING_UPDATER.get(this);
      updated = Arrays.copyOf(current, current.length + cursors.length);
      cursorValue = cursor.get();
      int index = current.length;
      for (AtomicLong c : cursors) {
        c.lazySet(cursorValue);
        updated[index++] = c;
      }
    } while (!GATING_UPDATER.compareAndSet(this, current, updated));
    cursorValue = cursor.get();
    for (AtomicLong c : cursors) {
      c.lazySet(cursorValue);
    }
  }

  long lastReleased(final long from, final long to) {
    for (long i = from; i <= to; i++) {
      if (availability.get(((int) i) & idxMask) != (int) (i >>> idxShift)) {
        return i - 1;
      }
    }
    return to;
  }

  long lastClaimed() {
    return cursor.get();
  }
}
