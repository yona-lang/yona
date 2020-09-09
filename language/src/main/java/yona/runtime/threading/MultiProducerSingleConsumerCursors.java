package yona.runtime.threading;

import java.util.concurrent.atomic.AtomicLong;

public final class MultiProducerSingleConsumerCursors extends MultiProducerCursors {
    private final AtomicLong consumerCursor = new AtomicLong(-1L);

    public MultiProducerSingleConsumerCursors(final int size) {
        super(size);
    }

    @Override
    protected boolean hasCapacity(int n, long start) {
        final long end = start + n - size;
        final long consumer = consumerCursor.getAcquire();
        if (start < consumer || consumer < end) {
            long min = Math.min(start, consumer);
            return end <= min;
        } else return true;
    }

    public SingleConsumer subscribe() {
        return new SingleConsumer(this, consumerCursor);
    }
}
