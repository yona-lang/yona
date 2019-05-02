package abzu.runtime.async;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.TruffleObject;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;

import static java.util.function.Function.identity;

@MessageResolution(receiverType = Promise.class)
public final class Promise implements TruffleObject {
  private static final AtomicReferenceFieldUpdater<Promise, Object> UPDATER = AtomicReferenceFieldUpdater.newUpdater(Promise.class, Object.class, "value");

  volatile Object value;

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
    fulfil(this, result).run();
  }

  private static Trampoline fulfil(Promise promise, Object result) {
    Object snapshot;
    do {
      snapshot = promise.value;
      if (!(snapshot instanceof Callback)) throw new AssertionError();
    } while (!UPDATER.compareAndSet(promise, snapshot, result));
    Trampoline trampoline = Trampoline.Done.INSTANCE;
    Callback callback = (Callback) snapshot;
    while (callback != Callback.Nil.INSTANCE) {
      if (callback instanceof Callback.Transform) {
        Callback.Transform transform = (Callback.Transform) callback;
        if (result instanceof Exception) {
          // result is an exception, no mapping is done, just fulfil callback's promise with it
          trampoline = new Trampoline.More(trampoline, () -> fulfil(transform.result, result));
        } else {
          try {
            Object o = transform.function.apply(result);
            if (o instanceof Promise) {
              // function returned a Promise, make it pass its result to callback's promise when done
              trampoline = new Trampoline.More(trampoline, () -> ((Promise) o).propagate(transform.result));
            } else {
              // otherwise, fulfil with what function returned
              trampoline = new Trampoline.More(trampoline, () -> fulfil(transform.result, o));
            }
          } catch (Exception e) {
            // function threw an exception, fulfil callbacks's promise with it
            trampoline = new Trampoline.More(trampoline, () -> fulfil(transform.result, e));
          }
        }
        callback = transform.next;
      } else {
        Callback.Consume consume = (Callback.Consume) callback;
        // just execute right here
        if (result instanceof Exception) consume.onFailure.accept(result);
        else consume.onSuccess.accept(result);
        callback = consume.next;
      }
    }
    return trampoline;
  }

  private Trampoline propagate(Promise promise) {
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (snapshot instanceof Callback) {
        // not yet
        update = new Callback.Transform(promise, identity(), (Callback) snapshot);
      } else {
        // already done
        final Object result = snapshot;
        return new Trampoline.More(Trampoline.Done.INSTANCE, () -> fulfil(promise, result));
      }
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return Trampoline.Done.INSTANCE;
  }

  public Promise map(Function<? super Object, ?> function) {
    Promise result = null;
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (snapshot instanceof Callback) {
        // promise is not fulfilled yet
        if (result == null) result = new Promise();
        update = new Callback.Transform(result, function, (Callback) snapshot);
      } else {
        // if this promise failed, propagate the exception
        if (snapshot instanceof Exception) return this;
        // if this promise succeeded, apply the function
        try {
          Object o = function.apply(snapshot);
          // if it's a promise, don't wrap it
          if (o instanceof Promise) return (Promise) o;
          // otherwise, fulfil the result with the value function returned
          if (result == null) result = new Promise();
          result.value = o;
          return result;
        } catch (Exception e) {
          // propagate the exception
          if (result == null) result = new Promise();
          result.value = e;
          return result;
        }
      }
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return result;
  }

  public Object mapUnwrap(Function<? super Object, ?> function) {
    Promise result = null;
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (snapshot instanceof Callback) {
        if (result == null) result = new Promise();
        update = new Callback.Transform(result, function, (Callback) snapshot);
      } else {
        if (snapshot instanceof Exception) return this;
        try {
          return function.apply(snapshot);
        } catch (Exception e) {
          return e;
        }
      }
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return result;
  }

  public Promise then(Promise promise) {
    Promise result = null;
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (snapshot instanceof Callback) {
        if (result == null) result = new Promise();
        update = new Callback.Transform(result, whatever -> promise, (Callback) snapshot);
      }
      else return snapshot instanceof Exception ? this : promise;
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return result;
  }

  public static Promise all(Object[] args) {
    Promise result = new Promise();
    Object[] data = args.clone();
    AtomicInteger counter = new AtomicInteger(data.length);
    for (int i = 0; i < data.length; i++) {
      Object arg = data[i];
      if (arg instanceof Promise) {
        int idx = i;
        Promise promise = (Promise) arg;
        Object snapshot;
        Object update;
        do {
          snapshot = promise.value;
          if (snapshot instanceof Callback) {
            update = new Callback.Consume(o -> {
              data[idx] = o;
              if (counter.decrementAndGet() == 0) result.fulfil(data);
            }, result::fulfil, (Callback) snapshot);
          } else {
            if (snapshot instanceof Exception) result.fulfil(snapshot);
            else {
              data[idx] = snapshot;
              if (counter.decrementAndGet() == 0) result.fulfil(data);
            }
            break;
          }
        } while (!UPDATER.compareAndSet(promise, snapshot, update));
      } else {
        if (counter.decrementAndGet() == 0) result.fulfil(data);
      }
    }
    return result;
  }

  public static Object await(Promise promise) {
    CountDownLatch latch = new CountDownLatch(1);
    Object snapshot;
    Object update;
    do {
      snapshot = promise.value;
      if (snapshot instanceof Callback) {
        update = new Callback.Consume(o -> latch.countDown(), e -> latch.countDown(), (Callback) snapshot);
      } else return snapshot;
    } while (!UPDATER.compareAndSet(promise, snapshot, update));
    try {
      latch.await();
    } catch (InterruptedException e) {
      return e;
    }
    return promise.value;
  }

  static boolean isInstance(TruffleObject promise) {
    return promise instanceof Promise;
  }

  private interface Callback {
    final class Transform implements Callback {
      final Promise result;
      final Function<? super Object, ?> function;
      final Callback next;

      Transform(Promise result, Function<? super Object, ?> function, Callback next) {
        this.result = result;
        this.function = function;
        this.next = next;
      }
    }

    final class Consume implements Callback {
      final Consumer<? super Object> onSuccess;
      final Consumer<? super Object> onFailure;
      final Callback next;

      Consume(Consumer<? super Object> onSuccess, Consumer<? super Object> onFailure, Callback next) {
        this.onSuccess = onSuccess;
        this.onFailure = onFailure;
        this.next = next;
      }
    }

    enum Nil implements Callback {
      INSTANCE
    }
  }

  private static abstract class Trampoline implements Runnable {
    static final class Done extends Trampoline {
      static final Done INSTANCE = new Done();

      @Override
      public void run() {}
    }

    static final class More extends Trampoline {
      final Trampoline prev;
      final Supplier<? extends Trampoline> step;

      More(Trampoline prev, Supplier<? extends Trampoline> step) {
        this.prev = prev;
        this.step = step;
      }

      @Override
      public void run() {
        Trampoline current = this;
        Stack stack = Stack.Nil.INSTANCE;
        while (true) {
          if (current instanceof More) {
            More more = (More) current;
            current = more.prev;
            stack = new Stack.Cons(more.step, stack);
          } else {
            if (stack instanceof Stack.Cons) {
              Stack.Cons cons = (Stack.Cons) stack;
              current = cons.head.get();
              stack = cons.tail;
            } else break;
          }
        }
      }
    }
  }

  private interface Stack {
    final class Cons implements Stack {
      final Supplier<? extends Trampoline> head;
      final Stack tail;

      Cons(Supplier<? extends Trampoline> head, Stack tail) {
        this.head = head;
        this.tail = tail;
      }
    }

    enum Nil implements Stack {
      INSTANCE
    }
  }
}
