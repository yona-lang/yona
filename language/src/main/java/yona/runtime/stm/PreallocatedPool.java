package yona.runtime.stm;

import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.function.Supplier;

// fixed-size pool of preallocated objects safe for use by multiple threads
// resetting object's state before it is released back to the pool has to be done by the user
final class PreallocatedPool<T> {
    final T[] objects;
    final AtomicIntegerArray claimed;
    final int size;

    @SuppressWarnings("unchecked")
    public PreallocatedPool(final int size, final Supplier<? extends T> allocator) {
        objects = (T[]) new Object[size];
        for (int i = 0; i < objects.length; i++) {
            objects[i] = allocator.get();
        }
        claimed = new AtomicIntegerArray(size);
        this.size = size;
    }

    // attempts to claim an object from the pool and returns its index or -1 if no unclaimed objects were found
    // start: index to start searching from
    public int tryClaim(final int start) {
        final int end = start + size;
        int index;
        for (int i = start; i < end; i++) {
            index = i % size;
            if (claimed.weakCompareAndSetAcquire(index, 0, 1)) {
                return index;
            }
        }
        return -1;
    }

    public T access(final int index) {
        return objects[index];
    }

    public void release(final int index) {
        claimed.set(index, 0);
    }
}
