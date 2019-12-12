package yatta.runtime.async;

import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaLanguage;
import yatta.ast.call.InvokeNode;
import yatta.ast.call.TailCallException;
import yatta.runtime.UndefinedNameException;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;

import static java.util.function.Function.identity;

@ExportLibrary(InteropLibrary.class)
public final class Promise implements TruffleObject {
  private static final AtomicReferenceFieldUpdater<Promise, Object> UPDATER = AtomicReferenceFieldUpdater.newUpdater(Promise.class, Object.class, "value");

  volatile Object value;

  private final InteropLibrary library;

  public Promise() {
    value = Callback.Nil.INSTANCE;
    library = InteropLibrary.getFactory().getUncached();
  }

  public Promise(InteropLibrary library) {
    value = Callback.Nil.INSTANCE;
    this.library = library;
  }

  public Promise(Object value) {
    this.value = value;
    library = InteropLibrary.getFactory().getUncached();
  }

  public void fulfil(Object result, Node node) {
    fulfil(this, result, node).run();
  }

  private Trampoline fulfil(Promise promise, Object result, Node node) {
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
        try {
          final Object o;
          if (result instanceof Throwable) {
            o = applyTCOToFunction(transform.onFailure, (Throwable) result, node);
          } else {
            o = applyTCOToFunction(transform.onSuccess, result, node);
          }
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
        callback = transform.next;
      } else {
        Callback.Consume consume = (Callback.Consume) callback;
        // just execute right here
        if (result instanceof Throwable) consume.onFailure.accept(result);
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
        update = new Callback.Transform(promise, identity(), identity(), (Callback) snapshot);
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
        update = new Callback.Transform(result, function, identity(), (Callback) snapshot);
      } else {
        // if this promise failed, propagate the exception
        if (snapshot instanceof Throwable) return this;
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

  public Promise map(Function<? super Object, ?> onSuccess, Function<? super Throwable, ?> onFailure, Node node) {
    Promise result = null;
    Object snapshot;
    Object update;
    do {
      snapshot = value;
      if (snapshot instanceof Callback) {
        // promise is not fulfilled yet
        if (result == null) result = new Promise();
        update = new Callback.Transform(result, onSuccess, onFailure, (Callback) snapshot);
      } else {
        try {
          final Object o;
          if (snapshot instanceof Throwable) {
            o = applyTCOToFunction(onFailure, (Throwable) snapshot, node);
          } else {
            o = applyTCOToFunction(onSuccess, snapshot, node);
          }
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
    if (value instanceof Callback || value instanceof Throwable) {
      return null;
    } else {
      return value;
    }
  }

  public Object unwrapWithError() {
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
            if (snapshot instanceof Throwable) result.fulfil(snapshot, node);
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

  public static Object await(Promise promise) throws Throwable {
    CountDownLatch latch = new CountDownLatch(1);
    Object snapshot;
    Object update;
    do {
      snapshot = promise.value;
      if (snapshot instanceof Callback) {
        update = new Callback.Consume(o -> latch.countDown(), e -> latch.countDown(), (Callback) snapshot);
      } else return throwIfThrowable(snapshot);
    } while (!UPDATER.compareAndSet(promise, snapshot, update));
    latch.await();
    return throwIfThrowable(promise.value);
  }

  private static Object throwIfThrowable(Object value) throws Throwable {
    if (value instanceof Throwable) throw (Throwable) value;
    return value;
  }

  private interface Callback {
    final class Transform implements Callback {
      final Promise result;
      final Function<? super Object, ?> onSuccess;
      final Function<? super Throwable, ?> onFailure;
      final Callback next;

      Transform(Promise result, Function<? super Object, ?> onSuccess, Function<? super Throwable, ?> onFailure, Callback next) {
        this.result = result;
        this.onSuccess = onSuccess;
        this.onFailure = onFailure;
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

  private <T, R> Object applyTCOToFunction(Function<? super T, ? extends R> function, T argument, Node node) {
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
        } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException ignored) {
          /* Execute was not successful. */
          throw UndefinedNameException.undefinedFunction(node, dispatchFunction);
        }
      }
    }
  }
}
