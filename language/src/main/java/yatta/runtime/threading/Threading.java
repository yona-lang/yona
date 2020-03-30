package yatta.runtime.threading;

import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.Function;
import yatta.runtime.async.TransactionalMemory;
import yatta.runtime.exceptions.UndefinedNameException;
import yatta.runtime.async.Promise;

import java.util.concurrent.atomic.AtomicIntegerFieldUpdater;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.LockSupport;
import java.util.concurrent.locks.ReentrantLock;

public final class Threading {
  static final AtomicIntegerFieldUpdater<Threading> WAITERS_UPDATER = AtomicIntegerFieldUpdater.newUpdater(Threading.class, "waiters");

  public static final ThreadLocal<TransactionalMemory.Transaction> TX = new ThreadLocal<>();

  static final int THREAD_COUNT = Math.max(2, Runtime.getRuntime().availableProcessors());
  static final int BUFFER_SIZE = 1024;
  static final int PRODUCE_SPIN_MAX_ATTEMPTS = 1000;
  static final int CONSUME_YIELD_MAX_ATTEMPTS = 10;
  static final int CONSUME_PARK_MAX_ATTEMPTS = 100;

  final Thread[] threads;
  final Consumer[] consumers;
  final RingBuffer<Task> ringBuffer;
  final Lock lock = new ReentrantLock();
  final Condition condition = lock.newCondition();

  volatile int waiters = 0;

  public Threading(TruffleLanguage.Env env) {
    ringBuffer = new RingBuffer<>(BUFFER_SIZE, Task::new);
    consumers = ringBuffer.subscribe(THREAD_COUNT);
    threads = new Thread[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
      Consumer consumer = consumers[i];
      threads[i] = env.createThread(() -> {
        final Consumer.Callback callback = new Consumer.Callback() {
          Promise promise;
          Function function;
          InteropLibrary dispatch;
          Node node;
          TransactionalMemory.Transaction tx;

          @Override
          void prepare(final long token) {
            final Task task = ringBuffer.read(token);
            promise = task.promise;
            function = task.function;
            dispatch = task.dispatch;
            node = task.node;
            tx = task.tx;
          }

          @Override
          void advance() {
            TX.set(tx);
            try {
              execute(promise, function, dispatch, node);
            } finally {
              promise = null;
              function = null;
              dispatch = null;
              node = null;
              TX.remove();
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
              continue ;
            }
            yields = 0;
            if (parks != CONSUME_PARK_MAX_ATTEMPTS) {
              LockSupport.parkNanos(1L);
              parks++;
              continue ;
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
      }, null, new ThreadGroup("yatta-worker"));
    }
  }

  public void initialize() {
    for (int i = 0; i < THREAD_COUNT; i++) {
      threads[i].start();
    }
  }

  public void submit(final Promise promise, final Function function, final InteropLibrary dispatch, final Node node) {
    int spins = 0;
    long token;
    while (true) {
      token = ringBuffer.tryClaim(1);
      if (token == -1) {
        if (spins != PRODUCE_SPIN_MAX_ATTEMPTS) {
          Thread.onSpinWait();
          spins++;
          continue;
        }
        spins = 0;
        execute(promise, function, dispatch, node);
      } else {
        break;
      }
    }
    Task task = ringBuffer.read(token);
    task.promise = promise;
    task.function = function;
    task.dispatch = dispatch;
    task.node = node;
    task.tx = TX.get();
    ringBuffer.release(token, token);
    if (waiters != 0) {
      lock.lock();
      try {
        condition.signal();
      } finally {
        lock.unlock();
      }
    }
    TX.remove();
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
    for (int i = 0; i < THREAD_COUNT; i++) {
      try {
        threads[i].interrupt();
        threads[i].join();
      } catch (InterruptedException e) {
        e.printStackTrace();
      }
    }
  }
}
