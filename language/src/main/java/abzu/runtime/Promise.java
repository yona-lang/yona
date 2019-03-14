package abzu.runtime;

import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import java.util.function.Function;

public final class Promise {
  private static final AtomicReferenceFieldUpdater<Promise, Object> UPDATER = AtomicReferenceFieldUpdater.newUpdater(Promise.class, Object.class, "value");

  private volatile Object value;

  public void fulfil(Object o) {
    Object snapshot;
    do {
      snapshot = value;
    } while (!UPDATER.compareAndSet(this, snapshot, o));
    try {
      ((Callbacks) snapshot).run(o);
    } catch (ClassCastException e) {
      throw new AssertionError();
    }
  }

  public Promise map(Function<? super Object, ?> function) {
    final Promise result = new Promise();
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (!(snapshot instanceof Callbacks) && snapshot != null) {
        result.fulfil(snapshot);
        break;
      }
      update = new Callbacks(function, result, (Callbacks) snapshot);
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return result;
  }

  private static final class Callbacks {
    final Function<? super Object, ?> function;
    final Promise promise;
    final Callbacks next;

    Callbacks(Function<? super Object, ?> function, Promise promise, Callbacks next) {
      this.function = function;
      this.promise = promise;
      this.next = next;
    }

    void run(Object result) {
      Callbacks cursor = this;
      do {
        promise.fulfil(function.apply(result));
        cursor = cursor.next;
      } while (cursor != null);
    }
  }
}
