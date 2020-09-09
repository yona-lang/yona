package yona.runtime.threading;

import java.util.concurrent.atomic.AtomicIntegerArray;
import java.util.concurrent.atomic.AtomicLong;

public abstract class MultiProducerCursors {
    private final AtomicLong producerCursor = new AtomicLong(-1L);
    private final int mask;
    private final int shift;
    private final AtomicIntegerArray availability;

    protected final int size;

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

    public final long tryClaim(final int n) {
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

    protected abstract boolean hasCapacity(final int n, final long start);

    public final int index(final long token) {
        return (int)(token & mask);
    }

    public final void release(final long from, final long to) {
        for (long i = from; i <= to; i++) {
            availability.setRelease(((int) i) & mask, (int) (i >>> shift));
        }
    }

    protected final long findLastReleased(final long from) {
        final long to = lastClaimed();
        for (long i = from; i <= to; i++) {
            if (availability.get(((int) i) & mask) != (int) (i >>> shift)) {
                return i - 1;
            }
        }
        return to;
    }

    protected final long lastClaimed() {
        return producerCursor.get();
    }
}
