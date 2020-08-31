package yona.runtime.async;

import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Supplier;

final class PreallocatedPool<E> {
  private final AtomicLong lastClaimed = new AtomicLong(-1L);
  private final E[] elements;
  private final AtomicIntegerArray availability;
  private final int size;

  @SuppressWarnings("unchecked")
  public PreallocatedPool(final int size, final Supplier<? extends E> allocator) {
    if (size <= 0) {
      throw new AssertionError();
    }
    elements = (E[]) new Object[size];
    for (int i = 0; i < elements.length; i++) {
      elements[i] = allocator.get();
    }
    availability = new AtomicIntegerArray(size);
    for (int i = 0; i < availability.length(); i++) {
      availability.set(i, 1);
    }
    this.size = size;
  }

  public int tryClaim() {
    final long current = lastClaimed.getAcquire();
    final long next = current + 1;
    for (long i = next; i < current + size; i++) {
      if (availability.compareAndSet((int) (i % size), 1, 0)) {
        lastClaimed.weakCompareAndSetRelease(current, i);
        return (int) (i % size);
      }
    }
    return -1;
  }

  public E access(final int slot) {
    return elements[slot];
  }

  public void release(final int slot) {
    availability.lazySet(slot, 1);
  }
}
