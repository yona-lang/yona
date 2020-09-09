package yona.runtime.threading;

import java.util.concurrent.atomic.AtomicLong;

public final class SingleConsumer {
    private final MultiProducerCursors producerCursors;
    private final AtomicLong cursor;

    public SingleConsumer(final MultiProducerCursors producerCursors, final AtomicLong cursor) {
        this.producerCursors = producerCursors;
        this.cursor = cursor;
    }

    public boolean consume(final Callback callback) {
        final long current = cursor.get();
        long next = current + 1;
        final long limit = producerCursors.findLastReleased(next);
        if (next <= limit) {
            long consumed;
            do {
                callback.execute(next, limit);
                consumed = next;
                next++;
            } while (next <= limit);
            cursor.set(consumed);
            return true;
        } else return false;
    }

    public static abstract class Callback {
        public abstract void execute(long token, long endToken);
    }
}
