package yona.runtime.network;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.Seq;
import yona.runtime.async.Promise;

import java.nio.channels.SelectionKey;

public class TCPConnection {
  private final static int MAX_RW_QUEUE_LENGTH = 16;

  public final SelectionKey selectionKey;
  public final InteropLibrary dispatch;
  public final Context context;
  public final Node node;

  public final NIOQueue<WriteRequest> writeQueue;
  public final NIOQueue<ReadRequest> readQueue;

  public TCPConnection(SelectionKey selectionKey, InteropLibrary dispatch, Context context, Node node) {
    this.selectionKey = selectionKey;
    this.dispatch = dispatch;
    this.context = context;
    this.node = node;
    this.writeQueue = new NIOQueue<>(WriteRequest.class, MAX_RW_QUEUE_LENGTH);
    this.readQueue = new NIOQueue<>(ReadRequest.class, MAX_RW_QUEUE_LENGTH);
  }

  public static final record WriteRequest(Seq buffer, Promise completedPromise) {
  }

  public static final record ReadRequest(Function untilCallback, Promise resultPromise) {
  }
}
