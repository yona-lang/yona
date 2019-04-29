package abzu.runtime.async;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.TruffleObject;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import java.util.function.Function;

import static java.util.function.Function.identity;

@MessageResolution(receiverType = Promise.class)
public final class Promise implements TruffleObject {
  private static final AtomicReferenceFieldUpdater<Promise, Object> UPDATER = AtomicReferenceFieldUpdater.newUpdater(Promise.class, Object.class, "value");

  private volatile Object value;

  public Promise() {
    value = Callback.Nil.INSTANCE;
  }

  Promise(Object value) {
    this.value = value;
  }

  @Override
  public ForeignAccess getForeignAccess() {
    return PromiseForeign.ACCESS;
  }

  public void fulfil(Object result) {
    Object snapshot;
    do {
      snapshot = value;
    } while (!UPDATER.compareAndSet(this, snapshot, result));
    if (snapshot instanceof Callback) {
      Frame frames = new Frame(result, (Callback) snapshot, null);
      do {
        while (frames.callback instanceof Callback.Cons) {
          Frame oldFrames = frames;
          Callback.Cons cons = (Callback.Cons) frames.callback;
          result = cons.function.apply(frames.result);
          if (cons.promise != null) {
            do {
              snapshot = cons.promise.value;
            } while (!UPDATER.compareAndSet(cons.promise, snapshot, result));
            if (snapshot instanceof Callback) {
              frames = new Frame(result, (Callback) snapshot, frames);
            }
          }
          oldFrames.callback = cons.next;
        }
        frames = frames.next;
      } while (frames != null);
    }
  }

  public Promise map(Function<? super Object, ?> function) {
    final Promise result = new Promise();
    Object o = attachCallback(function, result);
    if (o == null) return result;
    result.fulfil(o);
    return result;
  }

  public Object mapUnwrap(Function<? super Object, ?> function) {
    final Promise result = new Promise();
    Object o = attachCallback(function, result);
    return o != null ? o : result;
  }

  private Object attachCallback(Function<? super Object, ?> function, Promise promise) {
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (snapshot instanceof Callback) update = new Callback.Cons(function, promise, (Callback) snapshot);
      else if (snapshot instanceof RuntimeException) throw (RuntimeException) snapshot;
      else {
        Object result = function.apply(snapshot);
        if (result instanceof Promise) result = ((Promise) result).attachCallback(identity(), promise);
        return result == null ? promise : result;
      }
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return null;
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
        ((Promise) o).attachCallback(v -> {
          data[idx] = v;
          if (counter.decrementAndGet() == 0) result.fulfil(data);
          return null;
        }, null);
      } else {
        data[i] = o;
        if (counter.decrementAndGet() == 0) result.fulfil(data);
      }
    }
    return result;
  }

  public static Object await(Promise promise) throws InterruptedException {
    CountDownLatch cdl = new CountDownLatch(1);
    Object[] data = new Object[1];
    promise.attachCallback(v -> {
      data[0] = v;
      cdl.countDown();
      return null;
      }, null);
    cdl.await();
    return data[0];
  }

  static boolean isInstance(TruffleObject promise) {
    return promise instanceof Promise;
  }

  private interface Callback {
    final class Cons implements Callback {
      final Function<? super Object, ?> function;
      final Promise promise;
      final Callback next;

      Cons(Function<? super Object, ?> function, Promise promise, Callback next) {
        this.function = function;
        this.promise = promise;
        this.next = next;
      }
    }

    enum Nil implements Callback {
      INSTANCE
    }
  }

  private static final class Frame {
    final Object result;
    Callback callback;
    final Frame next;

    Frame(Object result, Callback callback, Frame next) {
      this.result = result;
      this.callback = callback;
      this.next = next;
    }
  }

}
