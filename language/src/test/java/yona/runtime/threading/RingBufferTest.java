package yona.runtime.threading;

import org.junit.jupiter.api.RepeatedTest;
import org.junit.jupiter.api.Tag;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.*;

public class RingBufferTest {
  private static final int N = 5;
  private static final int M = 2000000;
  private static final int S = 1 << 10;

  @RepeatedTest(25)
  @Tag("slow")
  public void testMultiConsumer() throws InterruptedException {
    final boolean[] values = new boolean[N * M];
    final AtomicInteger c = new AtomicInteger(0);
    int[] buffer = new int[S];
    final MultiProducerMultiConsumerCursors cursors = new MultiProducerMultiConsumerCursors(S);
    final int nThreads = Math.min(1, Runtime.getRuntime().availableProcessors());
    final MultiConsumer[] consumers = cursors.subscribe(nThreads);
    final CountDownLatch done = new CountDownLatch(nThreads);
    final Thread[] producerThreads = new Thread[nThreads];
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i] = new Thread(() -> {
        while (true) {
          int v = c.getAndIncrement();
          if (v >= N * M) {
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
          int value = -1;

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
      assertTrue(values[i]);
    }
  }

  @RepeatedTest(25)
  @Tag("slow")
  public void testSingleConsumer() throws InterruptedException {
    final int[] values = new int[N];
    final AtomicInteger c = new AtomicInteger(0);
    int[] buffer = new int[S];
    final MultiProducerSingleConsumerCursors cursors = new MultiProducerSingleConsumerCursors(S);
    final int nThreads = Math.min(2, Runtime.getRuntime().availableProcessors() / 2);
    final CountDownLatch done = new CountDownLatch(nThreads);
    final SingleConsumer consumer = cursors.subscribe();
    final Thread[] producerThreads = new Thread[nThreads];
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i] = new Thread(() -> {
        while (true) {
          int v = c.getAndIncrement();
          if (v >= N * M) {
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
        values[value % N] = values[value % N] + 1;
      }
    };
    while (done.getCount() != 0) {
      consumer.consume(callback);
    }
    for (int j = 0; j < S; j++) {
      consumer.consume(callback);
    }
    for (int i = 0; i < nThreads; i++) {
      producerThreads[i].join();
    }
    for (int i = 0; i < N; i++) {
      assertEquals(M, values[i]);
    }
  }
}
