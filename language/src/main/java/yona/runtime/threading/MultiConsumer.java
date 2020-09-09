package yona.runtime.threading;

import java.util.concurrent.atomic.AtomicLong;

public final class MultiConsumer {
    private final MultiProducerCursors producerCursors;
    private final AtomicLong sharedCursor;
    private final AtomicLong privateCursor;

    public MultiConsumer(final MultiProducerCursors producerCursors, final AtomicLong sharedCursor, final AtomicLong privateCursor) {
        this.producerCursors = producerCursors;
        this.sharedCursor = sharedCursor;
        this.privateCursor = privateCursor;
    }

    public boolean consume(final Callback callback) {
        long current;
        long next;
        long limit;
        do {
            current = sharedCursor.get();
            privateCursor.setRelease(current);
            next = current + 1;
            limit = producerCursors.findLastReleased(next);
            if (next <= limit) {
                callback.prepare(next);
            } else return false;
        } while (!sharedCursor.compareAndSet(current, next));
        callback.execute();
        return true;
    }

    public static abstract class Callback {
        public abstract void prepare(long token);

        public abstract void execute();
    }
}
