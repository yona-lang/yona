package yatta.runtime.async;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import static org.junit.Assert.assertEquals;

public class TransactionalMemoryTest {
  static final int N = 1 << 12;

  ExecutorService executorService;

  @BeforeEach
  public void setUp() {
    executorService = Executors.newFixedThreadPool(Runtime.getRuntime().availableProcessors());
  }

  @Test
  @Tag("slow")
  public void testSTM() throws InterruptedException {
    ExecutorService executorService = Executors.newFixedThreadPool(Runtime.getRuntime().availableProcessors());
    TransactionalMemory stm = new TransactionalMemory();
    List<TransactionalMemory.Var> vars = new ArrayList<>();
    for (int i = 0; i < N; i++) {
      vars.add(new TransactionalMemory.Var(stm, 0L));
    }
    CountDownLatch cdl = new CountDownLatch(N * N);
    for (int i = 0; i < vars.size(); i++) {
      final int idx = i;
      for (int j = 0; j < N; j++) {
        executorService.submit(() -> {
          TransactionalMemory.ReadWriteTransaction tx = new TransactionalMemory.ReadWriteTransaction(stm);
          while (true) {
            tx.start();
            TransactionalMemory.Var var = vars.get(idx);
            var.write(tx, ((long) var.read(tx, null)) + 1, null);
            for (int k = 0; k < idx; k++) {
              if (Integer.bitCount(k) == 1) {
                vars.get(k).protect(tx, null);
              }
            }
            if (tx.validate()) {
              tx.commit();
              break;
            } else {
              tx.abort();
              tx.reset();
            }
          }
          cdl.countDown();
        });
      }
    }
    cdl.await();
    for (int i = 0; i < N; i++) {
      assertEquals(((long) N), vars.get(i).read());
    }
  }

  @AfterEach
  public void tearDown() {
    executorService.shutdown();
  }
}
