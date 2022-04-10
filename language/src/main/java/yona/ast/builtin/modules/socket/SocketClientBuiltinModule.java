package yona.ast.builtin.modules.socket;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.ast.builtin.modules.BuiltinModule;
import yona.ast.builtin.modules.BuiltinModuleInfo;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.network.TCPClientChannel;
import yona.runtime.network.TCPConnection;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;

@BuiltinModuleInfo(packageParts = {"socket", "tcp"}, moduleName = "Client")
public final class SocketClientBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "connect")
  abstract static class ConnectBuiltin extends BuiltinNode {
    @Specialization
    public Object connect(Seq hostname, long port, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (port < 0L || port >= 65535L) {
        throw new BadArgException("port must be between 0 and 65535", this);
      }
      Context context = Context.get(this);
      try {
        SocketChannel clientSocketChannel = context.socketSelector.provider().openSocketChannel();
        clientSocketChannel.configureBlocking(false);
        SelectionKey selectionKey = clientSocketChannel.register(context.socketSelector, SelectionKey.OP_CONNECT);
        clientSocketChannel.connect(new InetSocketAddress(hostname.asJavaString(this), (int) port));
        TCPClientChannel channel = new TCPClientChannel(context, clientSocketChannel, selectionKey, this, dispatch);
        selectionKey.attach(channel);
        context.socketSelector.wakeup();
        return channel.yonaConnectionPromise.map((connection) -> new ConnectionContextManager((TCPConnection) connection, context), this);
      } catch (IOException e) {
        throw new yona.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @Override
  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(SocketClientBuiltinModuleFactory.ConnectBuiltinFactory.getInstance())
    );
  }
}
