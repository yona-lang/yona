package yona.ast.builtin.modules.socket;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.ast.builtin.modules.BuiltinModule;
import yona.ast.builtin.modules.BuiltinModuleInfo;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.network.YonaConnection;
import yona.runtime.network.YonaServerChannel;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;
import yona.runtime.threading.ExecutableFunction;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;

@BuiltinModuleInfo(packageParts = {"socket"}, moduleName = "Server")
public final class SocketServerBuiltinModule implements BuiltinModule {
  private static final class ChannelContextManager extends NativeObjectContextManager<YonaServerChannel> {
    public ChannelContextManager(YonaServerChannel yonaServerChannel, Context context) {
      super("server_channel", context.lookupGlobalFunction("socket\\Server", "run"), yonaServerChannel);
    }

    public static ChannelContextManager adapt(ContextManager<?> contextManager, Context context, Node node) {
      return new ChannelContextManager(((NativeObject<YonaServerChannel>) contextManager.getData(node)).getValue(), context);
    }
  }

  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(ContextManager<?> contextManager, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YonaLanguage.class) Context context) {
      ChannelContextManager connectionContextManager = ChannelContextManager.adapt(contextManager, context, this);
      boolean shouldClose = true;
      try {
        Object result = dispatch.execute(function);
        if (result instanceof Promise resultPromise) {
          shouldClose = false;
          return resultPromise.map(value -> {
            try {
              return value;
            } finally {
              closeChannel(connectionContextManager, value, this);
            }
          }, exception -> closeChannel(connectionContextManager, exception, this), this);
        } else {
          return result;
        }
      } catch (UnsupportedTypeException | UnsupportedMessageException | ArityException e) {
        throw new YonaException(e, this);
      } finally {
        if (shouldClose) {
          closeChannel(connectionContextManager, Unit.INSTANCE, this);
        }
      }
    }

    private static <T> T closeChannel(ChannelContextManager connectionContextManager, T result, Node node) {
      try {
        YonaServerChannel yonaServerChannel = connectionContextManager.nativeData(node);
        yonaServerChannel.serverSocketChannel.close();
        return result;
      } catch (IOException e) {
        throw new yona.runtime.exceptions.IOException(e, node);
      }
    }
  }

  @NodeInfo(shortName = "channel")
  abstract static class ChannelBuiltin extends BuiltinNode {
    @Specialization
    public Object channel(Seq hostname, long port, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (port < 0L || port >= 65535L) {
        throw new BadArgException("port must be between 0 and 65535", this);
      }
      try {
        ServerSocketChannel serverSocketChannel = context.socketSelector.provider().openServerSocketChannel();
        serverSocketChannel.bind(new InetSocketAddress(hostname.asJavaString(this), (int) port));
        serverSocketChannel.configureBlocking(false);
        SelectionKey selectionKey = serverSocketChannel.register(context.socketSelector, SelectionKey.OP_ACCEPT);
        YonaServerChannel result = new YonaServerChannel(context, serverSocketChannel, selectionKey, this, dispatch);
        selectionKey.attach(result);
        context.socketSelector.wakeup();
        return new ChannelContextManager(result, context);
      } catch (IOException e) {
        throw new yona.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "accept")
  abstract static class AcceptBuiltin extends BuiltinNode {
    @Specialization
    public Object accept(ContextManager<?> contextManager, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      ChannelContextManager connectionContextManager = ChannelContextManager.adapt(contextManager, context, this);
      YonaServerChannel yonaServerChannel = connectionContextManager.nativeData(this);
      Promise promise = new Promise(dispatch);
      yonaServerChannel.connectionPromises.submit(promise);
      return promise.map(yonaConnection -> new ConnectionContextManager((YonaConnection) yonaConnection, context), this);
    }
  }

  @Override
  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(SocketServerBuiltinModuleFactory.RunBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SocketServerBuiltinModuleFactory.ChannelBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SocketServerBuiltinModuleFactory.AcceptBuiltinFactory.getInstance()));
    return builtins;
  }
}
