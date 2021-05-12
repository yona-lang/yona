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
import yona.ast.builtin.utils.FlagsUtils;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.network.TCPConnection;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

import java.io.IOException;
import java.nio.channels.SelectionKey;

@BuiltinModuleInfo(packageParts = {"socket", "tcp"}, moduleName = "Connection")
public final class SocketConnectionBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(ContextManager<?> contextManager, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YonaLanguage.class) Context context) {
      ConnectionContextManager connectionContextManager = ConnectionContextManager.adapt(contextManager, context, this);
      boolean shouldClose = true;
      try {
        Object result = dispatch.execute(function);
        if (result instanceof Promise resultPromise) {
          shouldClose = false;
          return resultPromise.map(value -> {
            try {
              return value;
            } finally {
              closeConnection(connectionContextManager, value, this);
            }
          }, exception -> closeConnection(connectionContextManager, exception, this), this);
        } else {
          return result;
        }
      } catch (UnsupportedTypeException | UnsupportedMessageException | ArityException e) {
        throw new YonaException(e, this);
      } finally {
        if (shouldClose) {
          closeConnection(connectionContextManager, Unit.INSTANCE, this);
        }
      }
    }

    private static <T> T closeConnection(ConnectionContextManager connectionContextManager, T result, Node node) {
      try {
        TCPConnection TCPConnection = connectionContextManager.nativeData(node);
        TCPConnection.selectionKey.channel().close();
        return result;
      } catch (IOException e) {
        throw new yona.runtime.exceptions.IOException(e, node);
      }
    }
  }

  @NodeInfo(shortName = "close")
  abstract static class CloseBuiltin extends BuiltinNode {
    @Specialization
    public Object close(ContextManager<?> contextManager, @CachedContext(YonaLanguage.class) Context context) {
      ConnectionContextManager connectionContextManager = ConnectionContextManager.adapt(contextManager, context, this);
      try {
        TCPConnection TCPConnection = connectionContextManager.nativeData(this);
        TCPConnection.selectionKey.channel().close();
        return Unit.INSTANCE;
      } catch (IOException e) {
        throw new yona.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "read_until")
  abstract static class ReadUntilBuiltin extends BuiltinNode {
    @Specialization
    public Object readUntil(ContextManager<?> contextManager, Set flags, Function untilCallback, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YonaLanguage.class) Context context) {
      ConnectionContextManager connectionContextManager = ConnectionContextManager.adapt(contextManager, context, this);
      TCPConnection TCPConnection = connectionContextManager.nativeData(this);

      return FlagsUtils.withFlags(interestOpsFromFlags(flags, this), interestOps -> {
        int interestOpsOr = 0;

        for (Object interestOp : interestOps) {
          interestOpsOr |= (int) interestOp;
        }

        Promise promise = new Promise(dispatch);
        TCPConnection.readQueue.submit(new TCPConnection.ReadRequest(untilCallback, promise, interestOpsOr));
        context.socketSelector.wakeup();
        return promise;
      }, this);
    }
  }

  @NodeInfo(shortName = "write")
  abstract static class WriteBuiltin extends BuiltinNode {
    @Specialization
    public Object write(ContextManager<?> contextManager, Set flags, Seq data, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YonaLanguage.class) Context context) {
      ConnectionContextManager connectionContextManager = ConnectionContextManager.adapt(contextManager, context, this);
      TCPConnection TCPConnection = connectionContextManager.nativeData(this);

      return FlagsUtils.withFlags(interestOpsFromFlags(flags, this), interestOps -> {
        int interestOpsOr = 0;

        for (Object interestOp : interestOps) {
          interestOpsOr |= (int) interestOp;
        }

        Promise promise = new Promise(dispatch);
        TCPConnection.writeQueue.submit(new TCPConnection.WriteRequest(data, promise, interestOpsOr));
        context.socketSelector.wakeup();
        return promise;
      }, this);
    }
  }

  private static Object interestOpsFromFlags(Set flags, Node node) {
    return FlagsUtils.extractFlags(flags, flag -> switch (flag) {
      case "read" -> SelectionKey.OP_READ;
      case "write" -> SelectionKey.OP_WRITE;
      default -> throw new BadArgException("Invalid interest flag: " + flag, node);
    }, node);
  }

  @Override
  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(SocketConnectionBuiltinModuleFactory.CloseBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SocketConnectionBuiltinModuleFactory.RunBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SocketConnectionBuiltinModuleFactory.ReadUntilBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SocketConnectionBuiltinModuleFactory.WriteBuiltinFactory.getInstance()));
    return builtins;
  }
}
