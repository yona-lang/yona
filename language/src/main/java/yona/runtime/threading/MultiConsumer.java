package yona.runtime.threading;

import java.lang.invoke.VarHandle;
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
        VarHandle.fullFence();
        long current;
        long next;
        long limit;
        do {
            current = sharedCursor.getAcquire();
            privateCursor.setRelease(current);
            next = current + 1;
            limit = producerCursors.findLastReleased(next);
            if (next <= limit) {
                callback.prepare(next);
            } else return false;
        } while (!sharedCursor.weakCompareAndSetRelease(current, next));
        callback.execute();
        VarHandle.fullFence();
        return true;
    }

    public static abstract class Callback {
        // Two-step consumption: copy data from the slot into callback's fields
        // token - token of the currently consumed item
        // Prepare method might be called concurrently for several consumers, so nothing but copying data into fields should happen here
        public abstract void prepare(long token);

        // Two-step consumption: do the rest of the consumption
        // Because slot might already be in use at this point, only data previously copied to callback's fields should be accessed
        // Unlike prepare method, only one consumer will have this method called
        // Full fence follows this method's execution
        public abstract void execute();
    }
}
