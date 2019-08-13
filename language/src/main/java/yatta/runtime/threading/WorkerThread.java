package yatta.runtime.threading;


import yatta.runtime.Context;

import java.util.concurrent.BlockingQueue;

public class WorkerThread implements Runnable {
  private final BlockingQueue<Runnable> queue;
  private volatile boolean abort = false;

  public WorkerThread(BlockingQueue<Runnable> queue) {
    this.queue = queue;
  }

  @Override
  public void run() {
    try {
      while (true) {
        if   (abort) break;
        else queue.take().run();
      }
    } catch (InterruptedException ex) {
    }
  }

  public void abort() {
    abort = true;
  }
}
