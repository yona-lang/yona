package yatta.runtime.threading;

import com.oracle.truffle.api.TruffleLanguage;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

public final class Threading {
  private static final int THREAD_COUNT = Runtime.getRuntime().availableProcessors();
  private static final int QUEUE_SIZE = 1024;

  private Thread[] threads;
  private Worker[] workers;
  private BlockingQueue<Runnable> blockingQueue;

  public Threading(TruffleLanguage.Env env) {
    threads = new Thread[THREAD_COUNT];
    workers = new Worker[THREAD_COUNT];
    blockingQueue = new ArrayBlockingQueue<>(QUEUE_SIZE);
    for (int i = 0; i < THREAD_COUNT; i++) {
      workers[i] = new Worker(blockingQueue);
      threads[i] = env.createThread(workers[i]);
    }
  }

  public void initialize() {
    for (int i = 0; i < THREAD_COUNT; i++) {
      threads[i].start();
    }
  }

  public void submit(Runnable runnable) {
    blockingQueue.add(runnable);
  }

  public void dispose() {
    for (int i = 0; i < THREAD_COUNT; i++) {
      workers[i].abort();
      try {
        threads[i].interrupt();
        threads[i].join();
      } catch (InterruptedException e) {
        e.printStackTrace();
      }
    }
  }
}
