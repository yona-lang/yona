package yona.runtime.network;

import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import yona.TypesGen;
import yona.YonaException;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.*;

public final class NIOSelectorThread extends Thread {
  protected static final int SOCKET_READ_BUFFER_SIZE = 128;

  private final Context context;

  private volatile boolean closing = false;

  public NIOSelectorThread(Context context) {
    this.context = context;
    this.setName("yona-nio-selector");
  }

  @Override
  public void run() {
    while (!closing && context.socketSelector.isOpen()) {
      try {
        if (context.socketSelector.select() >= 0) {
          for (SelectionKey key : context.socketSelector.selectedKeys()) {
            if (key.isValid()) {
              if (key.isAcceptable()) {
                accept((TCPServerChannel) key.attachment(), key);
              } else if (key.isConnectable()) {
                connect((TCPClientChannel) key.attachment(), key);
              } else {
                SelectableChannel selectableChannel = key.channel();

                if (key.isReadable()) {
                  read((TCPConnection) key.attachment(), (SocketChannel) selectableChannel, key);
                }

                if (key.isValid() && key.isWritable()) {
                  write((TCPConnection) key.attachment(), (SocketChannel) selectableChannel, key);
                }
              }
            }
          }
          context.socketSelector.selectedKeys().clear();
        }
      } catch (IOException | InterruptedException e) {
        e.printStackTrace();
      }
    }

    if (closing) {
      try {
        context.socketSelector.close();
      } catch (IOException ignored) {
      }
    }
  }

  public void close() {
    this.closing = true;
  }

  // accept client connection
  private void accept(TCPServerChannel TCPServerChannel, SelectionKey acceptKey) throws IOException, InterruptedException {
    ServerSocketChannel serverSocketChannel = (ServerSocketChannel) acceptKey.channel();
    SocketChannel socketChannel = serverSocketChannel.accept();
    socketChannel.configureBlocking(false);
    SelectionKey readKey = socketChannel.register(context.socketSelector, SelectionKey.OP_READ | SelectionKey.OP_WRITE);
    YonaConnection yonaConnection = new YonaConnection(readKey, yonaServerChannel.dispatch, yonaServerChannel.context, yonaServerChannel.node);
    readKey.attach(yonaConnection);

    context.threading.submit(new Promise(yonaServerChannel.dispatch), new ExecutableFunction.JavaExecutableFunction(() -> {
      while (true) {
        Promise[] promises = yonaServerChannel.connectionPromises.consume();

        if (promises.length == 0) {
          continue;
        }

        assert promises.length == 1;

        promises[0].fulfil(yonaConnection, yonaConnection.node);
        break;
      }
      return Unit.INSTANCE;
    }, yonaServerChannel.node));
  }

  // connect to the server
  private void connect(TCPClientChannel TCPClientChannel, SelectionKey acceptKey) throws IOException, InterruptedException {
    try {
      SocketChannel socketChannel = (SocketChannel) acceptKey.channel();
      socketChannel.configureBlocking(false);
      if (!TCPClientChannel.yonaConnectionPromise.isFulfilled()) {
        while (socketChannel.isConnectionPending()) {
          socketChannel.finishConnect();
        }
        SelectionKey readKey = socketChannel.register(context.socketSelector, SelectionKey.OP_READ | SelectionKey.OP_WRITE);
        TCPConnection TCPConnection = new TCPConnection(readKey, TCPClientChannel.dispatch, TCPClientChannel.context, TCPClientChannel.node);
        TCPClientChannel.yonaConnectionPromise.fulfil(TCPConnection, TCPConnection.node);
        readKey.attach(TCPConnection);
      }
    } catch (IOException e) {
      TCPClientChannel.yonaConnectionPromise.fulfil(new yona.runtime.exceptions.IOException(e, TCPClientChannel.node), TCPClientChannel.node);
    }
  }

  private void read(YonaConnection yonaConnection, SocketChannel client, SelectionKey key) throws IOException, InterruptedException {
    for (YonaConnection.ReadRequest readRequest : yonaConnection.readQueue.consume()) {
      Seq result = Seq.EMPTY;
      Object finalResult;
      try {
        client.configureBlocking(false);
        ByteBuffer bf = ByteBuffer.allocate(SOCKET_READ_BUFFER_SIZE);
        int read;
        while (true) {
          bf.clear();
          read = client.read(bf);
          if (read <= 0) {
            break;
          }
          Object untilCallbackResult = yonaConnection.dispatch.execute(readRequest.untilCallback(), read);
          if (!(untilCallbackResult instanceof Boolean) && !(untilCallbackResult instanceof Promise)) { // TODO handle promise
            throw YonaException.typeError(yonaConnection.node, untilCallbackResult);
          }
          if (untilCallbackResult instanceof Promise) {
            // TODO
          }
          if (!(Boolean) untilCallbackResult) {
            break;
          }
          bf.flip();
          result = Seq.catenate(result, Seq.fromByteBuffer(bf));
        }
        key.interestOps(SelectionKey.OP_WRITE);

        finalResult = result;
      } catch (ClosedChannelException ignored) {
        finalResult = result;
      } catch (IOException e) {
        e.printStackTrace();
        finalResult = new yona.runtime.exceptions.IOException(e, yonaConnection.node);
      } catch (UnsupportedMessageException | UnsupportedTypeException | ArityException e) {
        finalResult = new BadArgException("Callback in read_until function must accept byte and return boolean (true if continue reading).", e, yonaConnection.node);
      }

      readRequest.resultPromise().fulfil(finalResult, yonaConnection.node);
    }
  }

  private void write(YonaConnection yonaConnection, SocketChannel client, SelectionKey key) throws IOException {
    for (YonaConnection.WriteRequest writeRequest : yonaConnection.writeQueue.consume()) {
      Object result;
      try {
        client.configureBlocking(false);
        Seq writeBuffer = writeRequest.buffer();
        client.write(writeBuffer.asByteBuffer(yonaConnection.node));
        key.interestOps(SelectionKey.OP_READ);

        result = writeBuffer;
      } catch (ClosedChannelException ignored) {
        result = Seq.EMPTY;
      } catch (IOException e) {
        e.printStackTrace();
        result = new yona.runtime.exceptions.IOException(e, yonaConnection.node);
      }

      writeRequest.completedPromise().fulfil(result, yonaConnection.node);
    }
  }
}
