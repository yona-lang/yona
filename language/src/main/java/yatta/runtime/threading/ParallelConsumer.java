package yatta.runtime.threading;

final class ParallelConsumer<E> {
  final RingBuffer<E> buffer;
  final CursorRead gate;
  final AtomicCursor sharedCursor;
  final AtomicCursor ownCursor;
  final StaticMembershipCursors groupCursor;
  final int id;

  ParallelConsumer(RingBuffer<E> buffer, StaticMembershipCursors groupCursor, int memberId, AtomicCursor sharedCursor, AtomicCursor ownCursor) {
    this.buffer = buffer;
    this.gate = buffer.cursor;
    this.groupCursor = groupCursor;
    this.sharedCursor = sharedCursor;
    this.ownCursor = ownCursor;
    id = memberId;
  }

  public State consume(Consume<E> with) {
    final long current = sharedCursor.readVolatile();
    final long next = current + 1;
    final long available = buffer.lastPublished(next, gate.readVolatile());
    if (next <= available) {
      long consumed = current;
      try {
        with.consume(buffer.slotFor(next), next != available);
        consumed = next;
      } finally {
        sharedCursor.writeOrdered(consumed);
        with.done();
      }
      return State.WORKING;
    } else if (buffer.cursor.readVolatile() >= next) {
      return State.GATING;
    } else {
      return State.IDLE;
    }
  }

  @SuppressWarnings("unchecked")
  static <E> ParallelConsumer<E>[] create(RingBuffer<E> buffer, int n) {
    if (n > 64) {
      throw new IllegalArgumentException("Can't have more than 64 parallel consumers in a group.");
    }
    ParallelConsumer<E>[] result = new ParallelConsumer[n];
    final AtomicCursor sharedCursor = new AtomicCursor();
    final AtomicCursor[] ownCursors = new AtomicCursor[n];
    for (int i = 0; i < n; i++) {
      ownCursors[i] = new AtomicCursor();
    }
    final StaticMembershipCursors groupCursor = new StaticMembershipCursors(sharedCursor, ownCursors);
    for (int i = 0; i < n; i++) {
      result[i] = new ParallelConsumer<E>(buffer, groupCursor, i, sharedCursor, ownCursors[i]);
    }
    return result;
  }

  enum State {
    IDLE, WORKING, GATING
  }

  static abstract class Consume<E> {
    abstract void consume(E data, boolean more);

    abstract void done();
  }
}
