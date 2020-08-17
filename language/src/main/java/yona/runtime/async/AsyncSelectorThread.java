package yona.runtime.async;

import java.io.IOException;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;

public final class AsyncSelectorThread implements Runnable {
  public Selector selector;

  @Override
  public void run() {
    try {
      selector = Selector.open();

      while (selector.select() != 0) {
        for (SelectionKey key : selector.selectedKeys()) {
          if (key.isAcceptable()) {
            // a connection was accepted by a ServerSocketChannel.

          } else if (key.isConnectable()) {
            // a connection was established with a remote server.

          } else if (key.isReadable()) {
            // a channel is ready for reading

          } else if (key.isWritable()) {
            // a channel is ready for writing
          }
        }
      }
    } catch (IOException e) {
      e.printStackTrace();
      System.exit(1);
    }
  }
}
