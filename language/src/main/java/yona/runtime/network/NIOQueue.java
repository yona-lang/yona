package yona.runtime.network;

import yona.runtime.threading.MultiProducerSingleConsumerCursors;
import yona.runtime.threading.SingleConsumer;

import java.lang.reflect.Array;

public final class NIOQueue<T> {
  public final MultiProducerSingleConsumerCursors queue;
  public final SingleConsumer consumer;
  public final ResultHolder callback;

  public final T[] items;

  @SuppressWarnings("unchecked")
  public NIOQueue(Class<T> itemsType, int maxLength) {
    this.queue = new MultiProducerSingleConsumerCursors(maxLength);
    this.consumer = queue.subscribe();
    this.items = (T[]) Array.newInstance(itemsType, maxLength);
    this.callback = new ResultHolder();
  }

  public final T consume() {
    return consumer.consume(callback) ? callback.value : null;
  }

  public final void submit(T item) {
    long token;
    while (true) {
      token = queue.tryClaim(1);
      if (token != -1L) {
        break;
      }
      Thread.yield();
    }
    items[queue.index(token)] = item;
    queue.release(token, token);
  }

  private final class ResultHolder extends SingleConsumer.Callback {
    T value;

    @Override
    public boolean execute(long token, long endToken) {
      value = items[queue.index(token)];
      return false;
    }
  }
}
