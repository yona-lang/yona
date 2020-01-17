package yatta.runtime.threading;

import org.junit.jupiter.api.Test;

import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.assertEquals;

public class RingBufferTest {
  private static final int N = 1 << 24;

  @Test
  public void testLoad() throws InterruptedException {
    final int[] values = new int[N];
    final AtomicInteger c = new AtomicInteger(0);
    RingBuffer<Int> buffer = new RingBuffer<>(1024, Int::new);
    final int m = Runtime.getRuntime().availableProcessors();
    final Consumer[] consumers = buffer.subscribe(m);
    final Thread[] threads = new Thread[m];
    for (int i = 0; i < m; i++) {
      final Consumer consumer = consumers[i];
      threads[i] = new Thread(new Runnable() {
        Consumer.Callback callback = new Consumer.Callback() {
          int value = Integer.MIN_VALUE;

          @Override
          void ready(final long token) {
            value = buffer.read(token).value;
          }

          @Override
          void released() {
            assertEquals(0, values[value]);
            values[value] = value;
          }
        };

        @Override
        public void run() {
          while (true) {
            long token = buffer.tryClaim(1);
            if (token != -1) {
              int v = c.getAndIncrement();
              if (v >= N) {
                break;
              }
              buffer.read(token).value = v;
              buffer.release(token, token);
            }
            consumer.consume(callback);
          }
        }
      });
    }
    for (int i = 0; i < m; i++) {
      threads[i].start();
    }
    for (int i = 0; i < m; i++) {
      threads[i].join();
    }
    for (int i = 0; i < m; i++) {
      assertEquals(i, values[i]);
    }
  }

  static final class Int {
    int value;
  }
}
