package yatta.runtime.threading;

import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.Function;
import yatta.runtime.UndefinedNameException;
import yatta.runtime.async.Promise;

import java.util.concurrent.atomic.AtomicIntegerFieldUpdater;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.LockSupport;
import java.util.concurrent.locks.ReentrantLock;

public final class Threading {
  static final AtomicIntegerFieldUpdater<Threading> WAITERS_UPDATER = AtomicIntegerFieldUpdater.newUpdater(Threading.class, "waiters");

  static final int THREAD_COUNT = Math.min(64, Runtime.getRuntime().availableProcessors());
  static final int BUFFER_SIZE = 1024;
  static final int YIELD_MAX_ATTEMPTS = 1;
  static final int PARK_MAX_ATTEMPTS = 10;

  final Thread[] threads;
  final ParallelConsumer<Task>[] consumers;
  final RingBuffer<Task> ringBuffer;
  final Lock lock = new ReentrantLock();
  final Condition condition = lock.newCondition();

  volatile int waiters = 0;

  public Threading(TruffleLanguage.Env env) {
    ringBuffer = new RingBuffer<>(BUFFER_SIZE, Task::new);
    consumers = ringBuffer.subscribe(THREAD_COUNT);
    threads = new Thread[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
      ParallelConsumer<Task> consumer = consumers[i];
      threads[i] = env.createThread(() -> {
        ParallelConsumer.Consume<Task> consume = new ParallelConsumer.Consume<>() {
          Promise promise;
          Function function;
          InteropLibrary dispatch;
          Node node;

          @Override
          void consume(Task task, boolean more) {
            promise = task.promise;
            function = task.function;
            dispatch = task.dispatch;
            node = task.node;
          }

          @Override
          void done() {
            execute(promise, function, dispatch, node);
            promise = null;
            function = null;
            dispatch = null;
            node = null;
          }
        };
        int yields = 0;
        int parks = 0;
        loop: while (true) {
          ParallelConsumer.State state = consumer.consume(consume);
          switch (state) {
            case GATING: {
              Thread.yield();
              break;
            }
            case IDLE: {
              if (yields != YIELD_MAX_ATTEMPTS) {
                Thread.yield();
                yields++;
                break;
              }
              yields = 0;
              if (parks != PARK_MAX_ATTEMPTS) {
                LockSupport.parkNanos(1L);
                parks++;
                break;
              }
              parks = 0;
              lock.lock();
              WAITERS_UPDATER.incrementAndGet(this);
              try {
                condition.await();
              } catch (InterruptedException e) {
                break loop;
              } finally {
                WAITERS_UPDATER.decrementAndGet(this);
                lock.unlock();
              }
              break;
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

  public void submit(Promise promise, Function function, InteropLibrary dispatch, Node node) {
    int yields = 0;
    int parks = 0;
    long token;
    while (true) {
      token = ringBuffer.tryClaim(1);
      if (token == -1) {
        if (yields != YIELD_MAX_ATTEMPTS) {
          Thread.yield();
          yields++;
          continue;
        }
        yields = 0;
        if (parks != PARK_MAX_ATTEMPTS) {
          LockSupport.parkNanos(1L);
          parks++;
          continue;
        }
        parks = 0;
        execute(promise, function, dispatch, node);
      } else {
        break;
      }
    }
    Task task = ringBuffer.slotFor(token);
    task.promise = promise;
    task.function = function;
    task.dispatch = dispatch;
    task.node = node;
    ringBuffer.publish(token, token);
    if (waiters != 0) {
      lock.lock();
      try {
        condition.signal();
      } finally {
        lock.unlock();
      }
    }
  }

  static void execute(Promise promise, Function function, InteropLibrary dispatch, Node node) {
    try {
      promise.fulfil(dispatch.execute(function), node);
    } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
      // Execute was not successful.
      promise.fulfil(UndefinedNameException.undefinedFunction(node, function), node);
    } catch (Throwable e) {
      promise.fulfil(e, node);
    }
  }

  public void dispose() {
    for (int i = 0; i < THREAD_COUNT; i++) {
      try {
        ringBuffer.unsubscribe(consumers[i]);
        threads[i].interrupt();
        threads[i].join();
      } catch (InterruptedException e) {
        e.printStackTrace();
      }
    }
  }
}
