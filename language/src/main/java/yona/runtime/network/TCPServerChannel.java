package yona.runtime.network;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Context;
import yona.runtime.async.Promise;

import java.nio.channels.SelectionKey;
import java.nio.channels.spi.AbstractSelectableChannel;

public class TCPServerChannel {
  public static final int MAX_CONNECTIONS = 1024;

  public final Context context;
  public final AbstractSelectableChannel serverSocketChannel;
  public final Node node;
  public final SelectionKey selectionKey;
  public final NIOQueue<Promise> connectionPromises;
  public final InteropLibrary dispatch;

  public TCPServerChannel(Context context, AbstractSelectableChannel serverSocketChannel, SelectionKey selectionKey, Node node, InteropLibrary dispatch) {
    this.context = context;
    this.serverSocketChannel = serverSocketChannel;
    this.selectionKey = selectionKey;
    this.node = node;
    this.dispatch = dispatch;
    this.connectionPromises = new NIOQueue<>(Promise.class, MAX_CONNECTIONS);
  }
}
