package yatta.runtime.async;

import yatta.ast.call.TailCallException;
import yatta.runtime.UndefinedNameException;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;

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

  public Promise(Object value) {
    this.value = value;
  }

  @Override
  public ForeignAccess getForeignAccess() {
    return PromiseForeign.ACCESS;
  }

  public void fulfil(Object result, Node node) {
    fulfil(this, result, node).run();
  }

  private static Trampoline fulfil(Promise promise, Object result, Node node) {
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
          trampoline = new Trampoline.More(trampoline, () -> fulfil(transform.result, result, node));
        } else {
          try {
            Object o = applyTCOToFunction(transform.function, result, node);
            if (o instanceof Promise) {
              // function returned a Promise, make it pass its result to callback's promise when done
              trampoline = new Trampoline.More(trampoline, () -> ((Promise) o).propagate(transform.result, node));
            } else {
              // otherwise, fulfil with what function returned
              trampoline = new Trampoline.More(trampoline, () -> fulfil(transform.result, o, node));
            }
          } catch (Exception e) {
            // function threw an exception, fulfil callbacks's promise with it
            trampoline = new Trampoline.More(trampoline, () -> fulfil(transform.result, e, node));
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

  private Trampoline propagate(Promise promise, Node node) {
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
        return new Trampoline.More(Trampoline.Done.INSTANCE, () -> fulfil(promise, result, node));
      }
    } while (!UPDATER.compareAndSet(this, snapshot, update));
    return Trampoline.Done.INSTANCE;
  }

  public Promise map(Function<? super Object, ?> function, Node node) {
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
          Object o = applyTCOToFunction(function, snapshot, node);
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

  /**
   * @return value of the promise, if it was fulfilled, otherwise returns null
   */
  public Object unwrap() {
    if (value instanceof Callback) {
      return null;
    } else {
      return value;
    }
  }

  public static Promise all(Object[] args, Node node) {
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
              if (counter.decrementAndGet() == 0) result.fulfil(data, node);
            }, (val) -> result.fulfil(val, node), (Callback) snapshot);
          } else {
            if (snapshot instanceof Exception) result.fulfil(snapshot, node);
            else {
              data[idx] = snapshot;
              if (counter.decrementAndGet() == 0) result.fulfil(data, node);
            }
            break;
          }
        } while (!UPDATER.compareAndSet(promise, snapshot, update));
      } else {
        if (counter.decrementAndGet() == 0) result.fulfil(data, node);
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
      public void run() {
      }
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

  private static <T, R> Object applyTCOToFunction(Function<T, R> function, T argument, Node node) {
    InteropLibrary library = InteropLibrary.getFactory().createDispatched(3);

    try {
      return function.apply(argument);
    } catch (TailCallException e) {
      yatta.runtime.Function dispatchFunction = e.function;
      Object[] argumentValues = e.arguments;
      while (true) {
        try {
          return library.execute(dispatchFunction, argumentValues);
        } catch (TailCallException te) {
          dispatchFunction = te.function;
          argumentValues = te.arguments;
        } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException _) {
          /* Execute was not successful. */
          // TODO add node
          throw UndefinedNameException.undefinedFunction(node, dispatchFunction);
        }
      }
    }
  }
}
