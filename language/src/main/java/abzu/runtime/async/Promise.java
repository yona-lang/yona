package abzu.runtime.async;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.TruffleObject;

import java.util.ArrayDeque;
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
    ArrayDeque<Completion> arg = new ArrayDeque<>();
    arg.add(new Completion(this, result));
    while (!arg.isEmpty()) arg = fulfil(new ArrayDeque<>(arg));
  }

  private static ArrayDeque<Completion> fulfil(ArrayDeque<Completion> in) {
    ArrayDeque<Completion> out = new ArrayDeque<>();
    for (Completion completion : in) {
      Object snapshot;
      do {
        snapshot = completion.promise.value;
      } while (!UPDATER.compareAndSet(completion.promise, snapshot, completion.result));
      Callback callbacks = (Callback) snapshot;
      while (callbacks instanceof Callback.Cons) {
        Callback.Cons cons = (Callback.Cons) callbacks;
        Object o;
        if (completion.result instanceof Exception) o = cons.onFailure.apply(completion.result);
        else o = cons.onSuccess.apply(completion.result);
        if (cons.promise != null) {
          if (o instanceof Promise) o = ((Promise) o).attachCallback(identity(), identity(), cons.promise);
          if (o != null) out.add(new Completion(cons.promise, o));
        }
        callbacks = cons.next;
      }
    }
    return out;
  }

  public Promise map(Function<? super Object, ?> function) {
    final Promise result = new Promise();
    Object o = attachCallback(function, identity(), result);
    if (o instanceof Promise) return (Promise) o;
    if (o != null) result.fulfil(o);
    return result;
  }

  public Object mapUnwrap(Function<? super Object, ?> function) {
    Promise result = new Promise();
    Object o = attachCallback(function, identity(), result);
    if (o == null) return result;
    if (o instanceof Promise) {
      result = (Promise) o;
      return result.value instanceof Callback ? result : result.value;
    } else return o;
  }

  // returns null if promise is not fulfilled yet, otherwise executes onSuccess/onFailure and returns the result
  private Object attachCallback(Function<? super Object, ?> onSuccess, Function<? super Object, ?> onFailure, Promise promise) {
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (snapshot instanceof Callback) update = new Callback.Cons(onSuccess, onFailure, promise, (Callback) snapshot);
      else return snapshot instanceof Exception ? onFailure.apply(snapshot) : onSuccess.apply(snapshot);
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
        }, e -> {
          result.fulfil(e);
          return null;
        }, null);
      } else {
        data[i] = o;
        if (counter.decrementAndGet() == 0) result.fulfil(data);
      }
    }
    return result;
  }

  public static Object await(Promise promise) {
    CountDownLatch cdl = new CountDownLatch(1);
    Object[] data = new Object[1];
    promise.attachCallback(v -> { data[0] = v; cdl.countDown(); return null; }, e -> { data[0] = e; cdl.countDown(); return null; }, null);
    try {
      cdl.await();
    } catch (InterruptedException e) {
      return e;
    }
    return data[0];
  }

  static boolean isInstance(TruffleObject promise) {
    return promise instanceof Promise;
  }

  private interface Callback {
    final class Cons implements Callback {
      final Function<? super Object, ?> onSuccess;
      final Function<? super Object, ?> onFailure;
      final Promise promise;
      final Callback next;

      Cons(Function<? super Object, ?> onSuccess, Function<? super Object, ?> onFailure, Promise promise, Callback next) {
        this.onSuccess = onSuccess;
        this.onFailure = onFailure;
        this.promise = promise;
        this.next = next;
      }
    }

    enum Nil implements Callback {
      INSTANCE
    }
  }

  /*private static abstract class Trampoline<V> implements Supplier<V> {

    Trampoline<V> flatMap(Function<? super V, ? extends Trampoline<V>> kleisli) {
      return new More<>(this, kleisli);
    }

    abstract boolean done();

    static <V> Trampoline<V> done(V value) {
      return new Done<>(value);
    }

    static <V> Trampoline<V> suspend(Supplier<Trampoline<V>> supplier) {
      return new More<>(Done.nothing(), nothing -> supplier.get());
    }

    static final class Done<V> extends Trampoline<V> {
      static final Done<?> NOTHING = new Done<>(new Object());

      final V value;

      Done(V value) {
        this.value = value;
      }

      @Override
      public V get() {
        return value;
      }

      @Override
      boolean done() {
        return true;
      }

      @SuppressWarnings("unchecked")
      static <V> Done<V> nothing() {
        return (Done<V>) NOTHING;
      }
    }

    static final class More<V> extends Trampoline<V> {
      final Trampoline<V> previous;
      final Function<? super V, ? extends Trampoline<V>> kleisli;

      More(Trampoline<V> previous, Function<? super V, ? extends Trampoline<V>> kleisli) {
        this.previous = previous;
        this.kleisli = kleisli;
      }

      @Override
      public V get() {
        Trampoline<V> current = this;
        Stack<Function<? super V, ? extends Trampoline<V>>> stack = Stack.nil();
        V result = null;
        while (result == null) {
          if (current.done()) {
            V value = current.get();
            if (stack instanceof Stack.Cons) {
              Stack.Cons<Function<? super V, ? extends Trampoline<V>>> cons = (Stack.Cons<Function<? super V, ? extends Trampoline<V>>>) stack;
              current = cons.head.apply(value);
              stack = cons.tail;
            } else {
              result = value;
            }
          } else {
            More<V> more = (More<V>) current;
            current = more.previous;
            stack = new Stack.Cons<>(more.kleisli, stack);
          }
        }
        return result;
      }

      @Override
      boolean done() {
        return false;
      }
    }
  }*/

  private static final class Completion {
    final Promise promise;
    final Object result;

    Completion(Promise promise, Object result) {
      this.promise = promise;
      this.result = result;
    }
  }

  /*private interface Stack<V> {
    final class Cons<V> implements Stack<V> {
      final V head;
      final Stack<V> tail;

      Cons(V head, Stack<V> tail) {
        this.head = head;
        this.tail = tail;
      }
    }

    enum Nil implements Stack<Object> {
      INSTANCE
    }

    @SuppressWarnings("unchecked")
    static <V> Stack<V> nil() {
      return (Stack<V>) Nil.INSTANCE;
    }
  }*/

}
