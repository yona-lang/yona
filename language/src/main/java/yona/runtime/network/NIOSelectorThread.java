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

    Promise promise = TCPServerChannel.connectionPromises.consume();

    if (promise != null) {
      try {
        SocketChannel socketChannel = serverSocketChannel.accept();
        socketChannel.configureBlocking(false);
        SelectionKey readKey = socketChannel.register(context.socketSelector, SelectionKey.OP_READ | SelectionKey.OP_WRITE);
        TCPConnection TCPConnection = new TCPConnection(readKey, TCPServerChannel.dispatch, TCPServerChannel.context, TCPServerChannel.node);
        readKey.attach(TCPConnection);
        promise.fulfil(TCPConnection, TCPConnection.node);
      } catch (IOException e) {
        promise.fulfil(new yona.runtime.exceptions.IOException(e, TCPServerChannel.node), TCPServerChannel.node);
      }
    }
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

  private void read(TCPConnection TCPConnection, SocketChannel client, SelectionKey key) throws IOException, InterruptedException {
    TCPConnection.ReadRequest readRequest = TCPConnection.readQueue.consume();
    if (readRequest == null) {
      return;
    }
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
        Object untilCallbackResult = TCPConnection.dispatch.execute(readRequest.untilCallback(), read);
        if (!(untilCallbackResult instanceof Boolean) && !(untilCallbackResult instanceof Promise)) { // TODO handle promise
          throw YonaException.typeError(TCPConnection.node, untilCallbackResult);
        }

        boolean keepReading;
        if (untilCallbackResult instanceof Promise untilCallbackResultPromise) {
          // this waiting is within the context of the NIO Selector thread. Not ideal, but probably the least harmful solution.
          // Actually mapping a promise at this point simply be too complicated to handle...
          Promise.await(untilCallbackResultPromise);
          keepReading = TypesGen.expectBoolean(untilCallbackResultPromise.unwrapOrThrow());
        } else {
          keepReading = (Boolean) untilCallbackResult;
        }

        if (!keepReading) {
          break;
        }

        bf.flip();
        result = Seq.catenate(result, Seq.fromByteBuffer(bf));
      }
      key.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);

      finalResult = result;
    } catch (ClosedChannelException ignored) {
      finalResult = result;
    } catch (IOException e) {
      e.printStackTrace();
      finalResult = new yona.runtime.exceptions.IOException(e, TCPConnection.node);
    } catch (UnsupportedMessageException | UnsupportedTypeException | ArityException e) {
      finalResult = new BadArgException("Callback in read_until function must accept byte and return boolean (true if continue reading).", e, TCPConnection.node);
    } catch (Throwable e) {
      finalResult = new YonaException(e, TCPConnection.node);
    }
    readRequest.resultPromise().fulfil(finalResult, TCPConnection.node);
  }

  private void write(TCPConnection TCPConnection, SocketChannel client, SelectionKey key) throws IOException {
    TCPConnection.WriteRequest writeRequest = TCPConnection.writeQueue.consume();
    if (writeRequest == null) {
      return;
    }
    Object result;
    try {
      client.configureBlocking(false);
      Seq writeBuffer = writeRequest.buffer();
      client.write(writeBuffer.asByteBuffer(TCPConnection.node));
      key.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);

      result = writeBuffer;
    } catch (ClosedChannelException ignored) {
      result = Seq.EMPTY;
    } catch (IOException e) {
      e.printStackTrace();
      result = new yona.runtime.exceptions.IOException(e, TCPConnection.node);
    }

    writeRequest.completedPromise().fulfil(result, TCPConnection.node);
  }
}
