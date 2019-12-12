package yatta.runtime.threading;


import java.util.concurrent.BlockingQueue;

public class Worker implements Runnable {
  private final BlockingQueue<Runnable> queue;
  private volatile boolean abort = false;

  public Worker(BlockingQueue<Runnable> queue) {
    this.queue = queue;
  }

  @Override
  public void run() {
    while (true) {
      try {
        queue.take().run();
      } catch (InterruptedException ex) {
        if (abort) break;
      }
    }
  }

  public void abort() {
    abort = true;
  }
}
