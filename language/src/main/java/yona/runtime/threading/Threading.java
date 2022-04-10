package yona.runtime.threading;

import com.oracle.truffle.api.CompilerDirectives;
import yona.YonaLanguage;
import yona.runtime.Context;
import yona.runtime.async.Promise;
import yona.runtime.network.NIOSelectorThread;

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

  final NIOSelectorThread NIOSelectorThread;
  final Thread[] threads;
  final Task[] ringBuffer;
  final MultiProducerMultiConsumerCursors ringBufferCursors;
  final MultiConsumer[] consumers;
  final Lock lock = new ReentrantLock();
  final Condition condition = lock.newCondition();

  volatile int waiters = 0;

  public Threading(final YonaLanguage language, final Context context) {
    NIOSelectorThread = new NIOSelectorThread(context);
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
          ExecutableFunction function;

          @Override
          public void prepare(final long token) {
            final Task task = ringBuffer[ringBufferCursors.index(token)];
            promise = task.promise;
            function = task.function;
          }

          @Override
          public void execute() {
            try {
              Threading.execute(promise, function);
            } finally {
              promise = null;
              function = null;
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
    NIOSelectorThread.start();
  }

  @CompilerDirectives.TruffleBoundary
  public Promise submit(final Promise promise, final ExecutableFunction function) {
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
        execute(promise, function);
        return promise;
      } else {
        break;
      }
    }
    Task task = ringBuffer[ringBufferCursors.index(token)];
    task.promise = promise;
    task.function = function;
    ringBufferCursors.release(token, token);
    if (waiters != 0) {
      lock.lock();
      try {
        condition.signal();
      } finally {
        lock.unlock();
      }
    }

    return promise;
  }

  static void execute(final Promise promise, final ExecutableFunction function) {
    function.execute(promise);
  }

  public void dispose() {
    for (int i = 0; i < CPU_THREADS; i++) {
      try {
        threads[i].interrupt();
        threads[i].join();
      } catch (InterruptedException ignored) {
      }
    }
    try {
      NIOSelectorThread.close();
      NIOSelectorThread.interrupt();
      NIOSelectorThread.join();
    } catch (InterruptedException ignored) {
    }
  }
}
