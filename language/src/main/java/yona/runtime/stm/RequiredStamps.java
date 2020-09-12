package yona.runtime.stm;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicLong;

// used to track stamps required by running transactions
// when transaction starts, it obtains its stamp by calling newStamp method
// counter is not incremented automatically though,
final class RequiredStamps {
    private static final VarHandle STAMPS;

    static {
        try {
            STAMPS = MethodHandles.lookup().findVarHandle(RequiredStamps.class, "stamps", AtomicLong[].class);
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }

    @SuppressWarnings({"FieldCanBeLocal", "FieldMayBeFinal"})
    private volatile AtomicLong[] stamps;

    // returns smallest value among stamps
    // counter - default value
    public long min(final AtomicLong counter) {
        long result = counter.get();
        final AtomicLong[] stamps = (AtomicLong[]) STAMPS.get(this);
        for (AtomicLong stamp : stamps) {
            result = Math.min(result, stamp.getAcquire());
        }
        return result;
    }

    // adds a new stamp
    // counter - to initialize new stamp with
    public AtomicLong newStamp(final AtomicLong counter) {
        final AtomicLong result = new AtomicLong(counter.getAcquire());
        AtomicLong[] expected;
        AtomicLong[] updated;
        do {
            expected = (AtomicLong[]) STAMPS.get(this);
            updated = Arrays.copyOf(expected, expected.length + 1);
            updated[expected.length] = result;
            result.setRelease(counter.getAcquire());
        } while (!STAMPS.compareAndSet(this, expected, updated));
        return result;
    }

    // removes previously added stamp
    public void remove(final AtomicLong stamp) {
        AtomicLong[] expected;
        AtomicLong[] updated;
        do {
            expected = (AtomicLong[]) STAMPS.get(this);
            updated = new AtomicLong[expected.length - 1];
            int updatedIdx = 0;
            for (AtomicLong element : expected) {
                //noinspection NumberEquality
                if (element != stamp) {
                    updated[updatedIdx++] = element;
                }
            }
        } while (!STAMPS.compareAndSet(this, expected, updated));
    }
}
