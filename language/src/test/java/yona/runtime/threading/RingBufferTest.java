package yona.runtime.threading;

import org.junit.jupiter.api.RepeatedTest;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.*;

public class RingBufferTest {
  private static final int N = 1 << 24;
  private static final int M = 1 << 10;

  @RepeatedTest(10)
  @Tag("slow")
  public void testMultiConsumer() throws InterruptedException {
    final boolean[] values = new boolean[N];
    final AtomicInteger c = new AtomicInteger(0);
    int[] buffer = new int[M];
    MultiProducerMultiConsumerCursors cursors = new MultiProducerMultiConsumerCursors(M);
    final int m = Math.max(1, Runtime.getRuntime().availableProcessors()/2);
    final MultiConsumer[] consumers = cursors.subscribe(m);
    final AtomicBoolean done = new AtomicBoolean();
    final Thread[] producerThreads = new Thread[m];
    for (int i = 0; i < m; i++) {
      producerThreads[i] = new Thread(() -> {
        while (!done.get()) {
          int v = c.getAndIncrement();
          if (v >= N) {
            done.set(true);
          } else {
            long token;
            do {
              token = cursors.tryClaim(1);
            } while (token == -1L);
            buffer[cursors.index(token)] = v;
            cursors.release(token, token);
          }
        }
      });
    }
    final Thread[] consumerThreads = new Thread[m];
    for (int i =0; i < m; i++) {
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
        while (!done.get()) {
          consumer.consume(callback);
        }
        for (int j =0; j < M; j++) {
          boolean consume;
          do {
            consume = consumer.consume(callback);
          } while (consume);
        }
      });
    }
    for (int i = 0; i < m; i++) {
      producerThreads[i].start();
    }
    for (int i = 0; i < m; i++) {
      consumerThreads[i].start();
    }
    for (int i = 0; i < m; i++) {
      producerThreads[i].join();
    }
    for (int i = 0; i < m; i++) {
      consumerThreads[i].join();
    }
    for (int i = 0; i < N; i++) {
      assertTrue(String.valueOf(i), values[i]);
    }
  }
}
