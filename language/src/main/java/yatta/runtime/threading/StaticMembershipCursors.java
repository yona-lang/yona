package yatta.runtime.threading;

import static java.lang.Math.min;

final class StaticMembershipCursors extends CursorWrite {
  final CursorWrite primary;
  final AtomicCursor[] secondary;

  StaticMembershipCursors(CursorWrite primary, AtomicCursor[] secondary) {
    this.primary = primary;
    this.secondary = secondary;
  }

  @Override
  long readVolatile() {
    long result = primary.readVolatile();
    for (CursorRead c : secondary) {
      result = min(result, c.readVolatile());
    }
    return result;
  }

  @Override
  void writeOrdered(long value) {
    primary.writeOrdered(value);
    long old;
    boolean done;
    for (AtomicCursor c : secondary) {
      do {
        old = c.readVolatile();
        if (old == Long.MAX_VALUE) {
          break;
        }
        done = c.compareAndSwap(old, value);
      } while (!done);
    }
  }
}
