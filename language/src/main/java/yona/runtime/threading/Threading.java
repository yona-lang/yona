package yona.runtime.threading;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Context;
import yona.runtime.Dict;
import yona.runtime.Function;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.UndefinedNameException;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicIntegerFieldUpdater;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.LockSupport;
import java.util.concurrent.locks.ReentrantLock;

public final class Threading {
  static final AtomicIntegerFieldUpdater<Threading> WAITERS_UPDATER = AtomicIntegerFieldUpdater.newUpdater(Threading.class, "waiters");

  public static final int CPU_THREADS = Math.max(1, Runtime.getRuntime().availableProcessors() - 1);
  public static final ThreadLocal<Integer> CPU_THREAD_ID = new ThreadLocal<>();

  static final AtomicInteger CPU_THREAD_ID_COUNTER = new AtomicInteger();
  static final int BUFFER_SIZE = 1024;
  static final int PRODUCE_SPIN_MAX_ATTEMPTS = 1000;
  static final int CONSUME_YIELD_MAX_ATTEMPTS = 10;
  static final int CONSUME_PARK_MAX_ATTEMPTS = 100;

  final Thread[] threads;
  final Task[] ringBuffer;
  final MultiProducerMultiConsumerCursors ringBufferCursors;
  final MultiConsumer[] consumers;
  final Lock lock = new ReentrantLock();
  final Condition condition = lock.newCondition();

  volatile int waiters = 0;

  public Threading(final Context context) {
    ringBuffer = new Task[BUFFER_SIZE];
    for (int i = 0; i < ringBuffer.length; i++) {
      ringBuffer[i] = new Task();
    }
    ringBufferCursors = new MultiProducerMultiConsumerCursors(BUFFER_SIZE);
    consumers = ringBufferCursors.subscribe(CPU_THREADS);
    threads = new Thread[CPU_THREADS];
    for (int i = 0; i < CPU_THREADS; i++) {
      MultiConsumer consumer = consumers[i];
      threads[i] = context.getEnv().createThread(() -> {
        CPU_THREAD_ID.set(CPU_THREAD_ID_COUNTER.incrementAndGet());
        final MultiConsumer.Callback callback = new MultiConsumer.Callback() {
          Promise promise;
          Function function;
          InteropLibrary dispatch;
          Node node;
          Dict localContexts;

          @Override
          public void prepare(final long token) {
            final Task task = ringBuffer[ringBufferCursors.index(token)];
            promise = task.promise;
            function = task.function;
            dispatch = task.dispatch;
            node = task.node;
            localContexts = task.localContexts;
          }

          @Override
          public void execute() {
            Context.LOCAL_CONTEXTS.set(localContexts);
            try {
              Threading.execute(promise, function, dispatch, node);
            } finally {
              promise = null;
              function = null;
              dispatch = null;
              node = null;
              Context.LOCAL_CONTEXTS.remove();
            }
          }
        };
        int yields = 0;
        int parks = 0;
        while (true) {
          if (!consumer.consume(callback)) {
            if (yields != CONSUME_YIELD_MAX_ATTEMPTS) {
              Thread.yield();
              yields++;
              continue;
            }
            yields = 0;
            if (parks != CONSUME_PARK_MAX_ATTEMPTS) {
              LockSupport.parkNanos(1L);
              parks++;
              continue;
            }
            parks = 0;
            lock.lock();
            WAITERS_UPDATER.incrementAndGet(this);
            try {
              condition.await();
            } catch (InterruptedException e) {
              break;
            } finally {
              WAITERS_UPDATER.decrementAndGet(this);
              lock.unlock();
            }
          }
        }
      }, null, new ThreadGroup("yona-worker"));
    }
  }

  public void initialize() {
    for (int i = 0; i < CPU_THREADS; i++) {
      threads[i].start();
    }
  }

  @CompilerDirectives.TruffleBoundary
  public void submit(final Promise promise, final Function function, final InteropLibrary dispatch, final Node node) {
    int spins = 0;
    long token;
    while (true) {
      token = ringBufferCursors.tryClaim(1);
      if (token == -1) {
        if (spins != PRODUCE_SPIN_MAX_ATTEMPTS) {
          Thread.onSpinWait();
          spins++;
          continue;
        }
        execute(promise, function, dispatch, node);
        return;
      } else {
        break;
      }
    }
    Task task = ringBuffer[ringBufferCursors.index(token)];
    task.promise = promise;
    task.function = function;
    task.dispatch = dispatch;
    task.node = node;
    task.localContexts = Context.LOCAL_CONTEXTS.get();
    ringBufferCursors.release(token, token);
    if (waiters != 0) {
      lock.lock();
      try {
        condition.signal();
      } finally {
        lock.unlock();
      }
    }
  }

  static void execute(final Promise promise, final Function function, final InteropLibrary dispatch, final Node node) {
    try {
      promise.fulfil(dispatch.execute(function), node);
    } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
      promise.fulfil(UndefinedNameException.undefinedFunction(node, function), node);
    } catch (Throwable e) {
      promise.fulfil(e, node);
    }
  }

  public void dispose() {
    for (int i = 0; i < CPU_THREADS; i++) {
      try {
        threads[i].interrupt();
        threads[i].join();
      } catch (InterruptedException e) {
        e.printStackTrace();
      }
    }
  }
}
