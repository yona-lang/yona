package yatta.runtime.threading;

import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;

import static java.lang.Math.min;
import static java.util.Arrays.copyOf;

final class DynamicMembershipCursors extends CursorRead {
  static final AtomicReferenceFieldUpdater<DynamicMembershipCursors, CursorRead[]> MEMBERS_UPDATER = AtomicReferenceFieldUpdater.newUpdater(DynamicMembershipCursors.class, CursorRead[].class, "members");

  volatile CursorRead[] members = new CursorRead[0];

  @Override
  long readVolatile() {
    long result = Long.MAX_VALUE;
    for (CursorRead cursor : members) {
      result = min(result, cursor.readVolatile());
    }
    return result;
  }

  void invite(CursorRead resetTo, CursorWrite member) {
    CursorRead[] current;
    CursorRead[] updated;
    long resetValue;
    do {
      current = members;
      updated = copyOf(current, current.length + 1);
      resetValue = resetTo.readVolatile();
      member.writeOrdered(resetValue);
      updated[current.length] = member;
    } while (!MEMBERS_UPDATER.compareAndSet(this, current, updated));
    resetValue = resetTo.readVolatile();
    member.writeOrdered(resetValue);
  }

  boolean expel(CursorWrite member) {
    CursorRead[] current;
    CursorRead[] updated;
    int deleteAt = -1;
    do {
      current = members;
      for (CursorRead c : current) {
        if (c == member) {
          deleteAt++;
        }
      }
      if (deleteAt == -1) return false;
      updated = new CursorRead[current.length - deleteAt + 1];
      int updatedIndex = 0;
      for (CursorRead c : current) {
        if (c != member) {
          updated[updatedIndex++] = c;
        }
      }
    } while (!MEMBERS_UPDATER.compareAndSet(this, current, updated));
    return true;
  }
}
