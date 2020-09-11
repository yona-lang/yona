package yona.runtime.threading;

import java.lang.invoke.VarHandle;
import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.concurrent.atomic.AtomicLong;

// base class for cursors to control inter-thread communication mechanism largely similar to LMAX Disruptor
// https://github.com/LMAX-Exchange/disruptor
// but with a few improvements and redesigned API
// actual buffer to share items between threads is to be allocated externally, this class only cares about its size and it has to be a power of two
public abstract class MultiProducerCursors {
    private final AtomicLong producerCursor = new AtomicLong(-1L);
    private final int mask;
    private final int shift;
    private final AtomicIntegerArray availability;

    protected final int size;

    /*
    capacity must be a power of two
     */
    protected MultiProducerCursors(final int capacity) {
        if (Integer.bitCount(capacity) != 1) {
            throw new AssertionError();
        }
        mask = capacity - 1;
        shift = 31 - Integer.numberOfLeadingZeros(capacity);
        availability = new AtomicIntegerArray(capacity);
        for (int i = 0; i < availability.length(); i++) {
            availability.set(i, -1);
        }
        size = capacity;
    }

    // Two-step production: try to claim a slot in the buffer
    // Returns a unique token by which data in this slot can be accessed or -1 if no free slots found
    public final long tryClaim(final int n) {
        VarHandle.fullFence();
        long current;
        long next;
        do {
            current = producerCursor.getAcquire();
            next = current + n;
            if (!hasCapacity(n, current)) {
                return -1L;
            }
        } while (!producerCursor.compareAndSet(current, next));
        return next;
    }

    // returns whether n items can be claimed from this buffer, start is the first token to search from
    protected abstract boolean hasCapacity(final int n, final long start);

    // returns an index in the externally allocated buffer to access data addressed by the provided token
    public final int index(final long token) {
        return (int)(token & mask);
    }

    // Two-step production: release slot(s) in the buffer, finishing the production
    // from, to - first and last tokens, inclusively, to be released. Allows batch production.
    public final void release(final long from, final long to) {
        for (long i = from; i <= to; i++) {
            availability.setRelease(((int) i) & mask, (int) (i >>> shift));
        }
    }

    // searches for last released token, from in the first token to search from
    protected final long findLastReleased(final long from) {
        final long to = lastClaimed();
        for (long i = from; i <= to; i++) {
            if (availability.getAcquire(((int) i) & mask) != (int) (i >>> shift)) {
                return i - 1;
            }
        }
        return to;
    }

    // returns last claimed token
    protected final long lastClaimed() {
        return producerCursor.getAcquire();
    }
}
