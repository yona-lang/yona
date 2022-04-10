package yona.ast.builtin.modules.socket;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.TypesGen;
import yona.YonaException;
import yona.ast.builtin.BuiltinNode;
import yona.ast.builtin.modules.BuiltinModule;
import yona.ast.builtin.modules.BuiltinModuleInfo;
import yona.ast.call.InvokeNode;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.network.TCPConnection;
import yona.runtime.network.TCPServerChannel;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;

@BuiltinModuleInfo(packageParts = {"socket", "tcp"}, moduleName = "Server")
public final class SocketServerBuiltinModule implements BuiltinModule {
  private static final class ChannelContextManager extends NativeObjectContextManager<TCPServerChannel> {
    public ChannelContextManager(TCPServerChannel TCPServerChannel, Context context) {
      super("server_channel", context.lookupGlobalFunction("socket\\tcp\\Server", "run"), TCPServerChannel);
    }

    public static ChannelContextManager adapt(ContextManager<?> contextManager, Context context, Node node) {
      return new ChannelContextManager(((NativeObject<TCPServerChannel>) contextManager.getData(NativeObject.class, node)).getValue(), context);
    }
  }

  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(ContextManager<?> contextManager, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      ChannelContextManager connectionContextManager = ChannelContextManager.adapt(contextManager, Context.get(this), this);
      boolean shouldClose = true;
      try {
        Object result = InvokeNode.dispatchFunction(function, dispatch, this);
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
      } finally {
        if (shouldClose) {
          closeChannel(connectionContextManager, Unit.INSTANCE, this);
        }
      }
    }

    private static <T> T closeChannel(ChannelContextManager connectionContextManager, T result, Node node) {
      try {
        TCPServerChannel TCPServerChannel = connectionContextManager.nativeData(node);
        TCPServerChannel.serverSocketChannel.close();
        return result;
      } catch (IOException e) {
        throw new yona.runtime.exceptions.IOException(e, node);
      }
    }
  }

  @NodeInfo(shortName = "channel")
  abstract static class ChannelBuiltin extends BuiltinNode {
    @Specialization
    public Object channel(Tuple args, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Context context = Context.get(this);
      Object unwrappedArgs = args.unwrapPromises(this);
      if (unwrappedArgs instanceof Promise unwrappedArgsPromise) {
        return unwrappedArgsPromise.map((resultArgs) -> createSocket((Object[]) resultArgs, context, dispatch), this);
      } else if (unwrappedArgs instanceof Object[]) {
        return createSocket((Object[]) unwrappedArgs, context, dispatch);
      } else {
        throw YonaException.typeError(this, unwrappedArgs);
      }
    }

    private ChannelContextManager createSocket(Object[] args, Context context, InteropLibrary dispatch) {
      if (args.length != 3) {
        throw new BadArgException("socket\\tcp::Channel expects triple of a type, host and port. Type must be :tcp", this);
      }

      Symbol socketType;
      try {
        socketType = TypesGen.expectSymbol(args[0]);
      } catch (UnexpectedResultException e) {
        throw new BadArgException("Expected symbol, got " + args[0], e, this);
      }

      switch (socketType.asString()) {
        case "tcp":
          Seq hostname;
          long port;
          try {
            hostname = TypesGen.expectSeq(args[1]);
          } catch (UnexpectedResultException e) {
            throw new BadArgException("Expected string, got: " + args[1], e, this);
          }
          try {
            port = TypesGen.expectLong(args[2]);
          } catch (UnexpectedResultException e) {
            throw new BadArgException("Expected integer, got: " + args[2], e, this);
          }

          return tcpSocket(hostname, port, context, dispatch);

//        case "unix":
//          // TODO implement
        default:
          throw new BadArgException("Unsupported socket type: " + socketType, this);
      }
    }

    private ChannelContextManager tcpSocket(Seq hostname, long port, Context context, InteropLibrary dispatch) {
      if (port < 0L || port >= 65535L) {
        throw new BadArgException("port must be between 0 and 65535", this);
      }
      try {
        ServerSocketChannel serverSocketChannel = context.socketSelector.provider().openServerSocketChannel();
        serverSocketChannel.configureBlocking(false);
        SelectionKey selectionKey = serverSocketChannel.register(context.socketSelector, SelectionKey.OP_ACCEPT);
        serverSocketChannel.bind(new InetSocketAddress(hostname.asJavaString(this), (int) port));
        TCPServerChannel result = new TCPServerChannel(context, serverSocketChannel, selectionKey, this, dispatch);
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
    public Object accept(ContextManager<?> contextManager, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Context context = Context.get(this);
      ChannelContextManager connectionContextManager = ChannelContextManager.adapt(contextManager, context, this);
      TCPServerChannel TCPServerChannel = connectionContextManager.nativeData(this);
      Promise promise = new Promise(dispatch);
      TCPServerChannel.connectionPromises.submit(promise);
      context.socketSelector.wakeup();
      return promise.map(connection -> new ConnectionContextManager((TCPConnection) connection, context), this);
    }
  }

  @Override
  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(SocketServerBuiltinModuleFactory.RunBuiltinFactory.getInstance()),
        new ExportedFunction(SocketServerBuiltinModuleFactory.ChannelBuiltinFactory.getInstance()),
        new ExportedFunction(SocketServerBuiltinModuleFactory.AcceptBuiltinFactory.getInstance())
    );
  }
}
