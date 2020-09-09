package yona.runtime.threading;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicLong;

public final class MultiProducerMultiConsumerCursors extends MultiProducerCursors {
    private static final VarHandle CONSUMERS;

    static {
        try {
            CONSUMERS = MethodHandles.lookup().findVarHandle(MultiProducerMultiConsumerCursors.class, "consumers", AtomicLong[].class);
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }

    private final AtomicLong cachedSlowestConsumer = new AtomicLong(-1L);
    @SuppressWarnings({"FieldMayBeFinal", "MismatchedReadAndWriteOfArray"})
    private volatile AtomicLong[] consumers = new AtomicLong[]{};

    public MultiProducerMultiConsumerCursors(final int size) {
        super(size);
    }

    @Override
    protected boolean hasCapacity(final int n, final long start) {
        final long end = start + n - size;
        final long consumer = cachedSlowestConsumer.get();
        if (start < consumer || consumer < end) {
            long min = start;
            for (AtomicLong cursor : consumers) {
                min = Math.min(min, cursor.get());
            }
            cachedSlowestConsumer.set(min);
            return end <= min;
        } else return true;
    }

    public MultiConsumer[] subscribe(final int n) {
        final AtomicLong sharedCursor = new AtomicLong(-1L);
        final AtomicLong[] privateCursors = new AtomicLong[n];
        for (int i = 0; i < n; i++) {
            privateCursors[i] = new AtomicLong(-1L);
        }
        final MultiConsumer[] result = new MultiConsumer[n];
        for (int i = 0; i < n; i++) {
            result[i] = new MultiConsumer(this, sharedCursor, privateCursors[i]);
        }
        invite(privateCursors);
        return result;
    }

    private void invite(final AtomicLong[] cursors) {
        long cursorValue;
        AtomicLong[] expected;
        AtomicLong[] updated;
        do {
            expected = (AtomicLong[]) CONSUMERS.get(this);
            updated = Arrays.copyOf(expected, expected.length + cursors.length);
            cursorValue = lastClaimed();
            int index = expected.length;
            for (AtomicLong cursor : cursors) {
                cursor.set(cursorValue);
                updated[index++] = cursor;
            }
        } while (!CONSUMERS.compareAndSet(this, expected, updated));
        cursorValue = lastClaimed();
        for (AtomicLong cursor : cursors) {
            cursor.set(cursorValue);
        }
    }
}
