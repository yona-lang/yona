package yatta.runtime.async;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import static org.junit.Assert.assertEquals;

public class STMTest {
  static final int N = 512;

  @Test
  public void testSTM() throws InterruptedException {
    ExecutorService executorService = Executors.newFixedThreadPool(Runtime.getRuntime().availableProcessors() * 2);
    STM stm = new STM();
    List<STM.Var> vars = new ArrayList<>();
    for (int i = 0; i < N; i++) {
      vars.add(new STM.Var(0L));
    }
    CountDownLatch cdl = new CountDownLatch(N * N);
    for (int i = 0; i < N * N; i++) {
      final int v = i % N;
      executorService.submit(() -> {
        STM.Transaction tx;
        boolean success = false;
        do {
          tx = stm.newTransaction(false);
          try {
            for (int j = 0; j <= v; j++) {
              if (j == v) {
                vars.get(0).ensure(tx);
              }
              final STM.Var var = vars.get(j);
              long value = (long) var.read(tx);
              var.write(value + 1, tx);
            }
            if (tx.validate()) {
              tx.commit();
              success = true;
            } else {
              tx.abort();
            }
          } catch (Exception e) {
            tx.abort();
          }
        } while (!success);
        cdl.countDown();
      });
    }
    cdl.await();
    for (int i = 0; i < N; i++) {
      assertEquals(((long) N * (N - i)), vars.get(i).read());
    }
    executorService.shutdown();
  }
}
