package yona.runtime.network;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Context;
import yona.runtime.async.Promise;

import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;

public class YonaClientChannel {
  public final Context context;
  public final SocketChannel clientSocketChannel;
  public final Node node;
  public final Promise yonaConnectionPromise;
  public final SelectionKey selectionKey;
  public final InteropLibrary dispatch;

  public YonaClientChannel(Context context, SocketChannel clientSocketChannel, SelectionKey selectionKey, Node node, InteropLibrary dispatch) {
    this.context = context;
    this.clientSocketChannel = clientSocketChannel;
    this.selectionKey = selectionKey;
    this.node = node;
    this.dispatch = dispatch;
    this.yonaConnectionPromise = new Promise(dispatch);
  }
}
