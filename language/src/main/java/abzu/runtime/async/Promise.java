package abzu.runtime.async;

import abzu.AbzuException;
import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.TruffleObject;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Function;

import static java.util.function.Function.identity;

@MessageResolution(receiverType = Promise.class)
public final class Promise implements TruffleObject {
  private static final AtomicReferenceFieldUpdater<Promise, Object> UPDATER = AtomicReferenceFieldUpdater.newUpdater(Promise.class, Object.class, "value");

  @Override
  public ForeignAccess getForeignAccess() {
    return PromiseForeign.ACCESS;
  }

  static boolean isInstance(TruffleObject promise) {
    return promise instanceof Promise;
  }

  public Promise() {
    value = null;
  }

  Promise(Object value) {
    this.value = value;
  }

  private volatile Object value;

  public void fulfil(Object o) {
    Object snapshot;
    do {
      snapshot = value;
    } while (!UPDATER.compareAndSet(this, snapshot, o));
    if (snapshot instanceof Callback && !(o instanceof AbzuException)) {
      ((Callback) snapshot).run(o);
    }
  }

  private Object attachCallback(Function<? super Object, ?> function, Promise promise) {
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (!(snapshot instanceof Callback) && snapshot != null) {
        Object o = function.apply(snapshot);
        if (!(o instanceof Promise)) return o;
        o = ((Promise) o).attachCallback(identity(), promise);
        return o != null ? o : promise;
      }
      update = new Callback(function, promise, (Callback) snapshot);
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return null;
  }

  public Promise mapPure(Function<? super Object, ?> function) {
    final Promise result = new Promise();
    Object o = attachCallback(function, result);
    if (o == null) return result;
    result.fulfil(o);
    return result;
  }

  public Object map(Function<? super Object, ?> function) {
    final Promise result = new Promise();
    Object o = attachCallback(function, result);
    return o != null ? o : result;
  }

  public static Promise all(Object[] os) {
    Promise result = new Promise();
    Object[] data = new Object[os.length];
    AtomicInteger counter = new AtomicInteger(os.length);
    Object o;
    for (int i = 0; i < os.length; i++) {
      o = os[i];
      if (o instanceof Promise) {
        final int idx = i;
        ((Promise) o).map(v -> {
          data[idx] = v;
          if (counter.decrementAndGet() == 0) result.fulfil(data);
          return v;
        });
      } else {
        data[i] = o;
        if (counter.decrementAndGet() == 0) result.fulfil(data);
      }
    }
    return result;
  }

  public static Object await(Promise promise) {
    Object result;
    while (true) {
      result = promise.value;
      if (!(result instanceof Callback) && result != null) break;
      LockSupport.parkNanos(1);
    }
    return result;
  }

  private static final class Callback {
    final Function<? super Object, ?> function;
    final Promise promise;
    final Callback next;

    Callback(Function<? super Object, ?> function, Promise promise, Callback next) {
      this.function = function;
      this.promise = promise;
      this.next = next;
    }

    void run(Object result) {
      Callback cursor = this;
      do {
        Object o = function.apply(result);
        if (o instanceof Promise) {
          o = ((Promise) o).attachCallback(identity(), promise);
          if (o != null) promise.fulfil(o);
        } else promise.fulfil(o);
        cursor = cursor.next;
      } while (cursor != null);
    }
  }
}
