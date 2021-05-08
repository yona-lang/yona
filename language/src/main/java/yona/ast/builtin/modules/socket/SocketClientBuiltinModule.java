package yona.ast.builtin.modules.socket;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.ast.builtin.modules.BuiltinModule;
import yona.ast.builtin.modules.BuiltinModuleInfo;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.async.Promise;
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
    public Object connect(Seq hostname, long port, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (port < 0L || port >= 65535L) {
        throw new BadArgException("port must be between 0 and 65535", this);
      }
      try {
        SocketChannel clientSocketChannel = context.socketSelector.provider().openSocketChannel();
        clientSocketChannel.configureBlocking(false);
        clientSocketChannel.connect(new InetSocketAddress(hostname.asJavaString(this), (int) port));
        SelectionKey selectionKey = clientSocketChannel.register(context.socketSelector, SelectionKey.OP_CONNECT);
        TCPClientChannel TCPClientChannel = new TCPClientChannel(context, clientSocketChannel, selectionKey, this, dispatch);
        selectionKey.attach(TCPClientChannel);
        Promise result = TCPClientChannel.yonaConnectionPromise.map((yonaConnection) -> new ConnectionContextManager((TCPConnection) yonaConnection, context), this);
        context.socketSelector.wakeup();
        return result;
      } catch (IOException e) {
        throw new yona.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @Override
  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(SocketClientBuiltinModuleFactory.ConnectBuiltinFactory.getInstance()));
    return builtins;
  }
}
