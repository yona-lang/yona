package yona.runtime.threading;

import org.junit.jupiter.api.RepeatedTest;
import org.junit.jupiter.api.Tag;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.*;

public class RingBufferTest {
  private static final int N = 1000000;
  private static final int M = 1 << 10;

  @RepeatedTest(50)
  @Tag("slow")
  public void testMultiConsumer() throws InterruptedException {
    final boolean[] values = new boolean[N];
    final AtomicInteger c = new AtomicInteger(0);
    int[] buffer = new int[M];
    final MultiProducerMultiConsumerCursors cursors = new MultiProducerMultiConsumerCursors(M);
    final int nThreads = Math.min(2, Runtime.getRuntime().availableProcessors() / 2);
    final MultiConsumer[] consumers = cursors.subscribe(nThreads);
    final CountDownLatch done = new CountDownLatch(nThreads);
    final Thread[] producerThreads = new Thread[nThreads];
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i] = new Thread(() -> {
        while (true) {
          int v = c.getAndIncrement();
          if (v >= N) {
            done.countDown();
            break;
          } else {
            long token;
            while (true) {
              token = cursors.tryClaim(1);
              if (token != -1L) {
                break;
              }
              Thread.yield();
            }
            buffer[cursors.index(token)] = v;
            cursors.release(token, token);
          }
        }
      });
    }
    final Thread[] consumerThreads = new Thread[nThreads];
    for (int i =0; i < nThreads; i++) {
      final MultiConsumer consumer = consumers[i];
      consumerThreads[i] = new Thread(() -> {
        final MultiConsumer.Callback callback = new MultiConsumer.Callback() {
          int value = Integer.MIN_VALUE;

          @Override
          public void prepare(final long token) {
            value = buffer[cursors.index(token)];
          }

          @Override
          public void execute() {
            values[value] = !values[value];
          }
        };
        while (done.getCount() != 0) {
          consumer.consume(callback);
        }
        for (int j = 0; j < M; j++) {
          consumer.consume(callback);
        }
      });
    }
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i].start();
    }
    for (int i = 0; i < nThreads; i++) {
      consumerThreads[i].start();
    }
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i].join();
    }
    for (int i = 0; i < nThreads; i++) {
      consumerThreads[i].join();
    }
    for (int i = 0; i < N; i++) {
      assertTrue(String.valueOf(i), values[i]);
    }
  }

  @RepeatedTest(50)
  @Tag("slow")
  public void testSingleConsumer() throws InterruptedException {
    final boolean[] values = new boolean[N];
    final AtomicInteger c = new AtomicInteger(0);
    int[] buffer = new int[M];
    final MultiProducerSingleConsumerCursors cursors = new MultiProducerSingleConsumerCursors(M);
    final int nThreads = Math.min(2, Runtime.getRuntime().availableProcessors() / 2);
    final CountDownLatch done = new CountDownLatch(nThreads);
    final SingleConsumer consumer = cursors.subscribe();
    final Thread[] producerThreads = new Thread[nThreads];
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i] = new Thread(() -> {
        while (true) {
          int v = c.getAndIncrement();
          if (v >= N) {
            done.countDown();
            break;
          } else {
            long token;
            while (true) {
              token = cursors.tryClaim(1);
              if (token != -1L) {
                break;
              }
              Thread.yield();
            }
            buffer[cursors.index(token)] = v;
            cursors.release(token, token);
          }
        }
      });
    }
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i].start();
    }
    final SingleConsumer.Callback callback = new SingleConsumer.Callback() {
      @Override
      public void execute(long token, long endToken) {
        final int value = buffer[cursors.index(token)];
        values[value] = !values[value];
      }
    };
    while (done.getCount() != 0) {
      consumer.consume(callback);
    }
    for (int j = 0; j < M; j++) {
      consumer.consume(callback);
    }
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i].join();
    }
    for (int i = 0; i < N; i++) {
      assertTrue(String.valueOf(i), values[i]);
    }
  }
}
