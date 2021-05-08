package yona.runtime.network;

import yona.runtime.threading.MultiProducerSingleConsumerCursors;
import yona.runtime.threading.SingleConsumer;

import java.lang.reflect.Array;
import java.util.Arrays;

public final class NIOQueue<T> {
  public final MultiProducerSingleConsumerCursors queue;
  public final SingleConsumer consumer;

  public final T[] items;

  public NIOQueue(Class<T> itemsType, int maxLength) {
    this.queue = new MultiProducerSingleConsumerCursors(maxLength);
    this.consumer = queue.subscribe();
    this.items = (T[]) Array.newInstance(itemsType, maxLength);
    this.itemElementType = itemsType;
  }

  private final class ConsumeResult {
    public T[] results;

    private ConsumeResult() {
      this.results = (T[]) Array.newInstance(itemElementType, 0);
    }
  }

  public final T[] consume() {
    ConsumeResult result = new ConsumeResult();

    SingleConsumer.Callback consumeCallback = new SingleConsumer.Callback() {
      @Override
      public void execute(long token, long endToken) {
        result.results = Arrays.copyOfRange(items, (int) token, (int) endToken + 1);
      }
    };

    consumer.consume(consumeCallback);

    return result.results;
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
}
