package yona.ast.builtin.modules;

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
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.async.TransactionalMemory;
import yona.runtime.exceptions.STMException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;
import yona.runtime.stdlib.PrivateFunction;

@BuiltinModuleInfo(moduleName = "STM")
public class STMBuiltinModule implements BuiltinModule {
  private static final String TX_CONTEXT_NAME = "tx";

  protected static final class STMContextManager extends NativeObjectContextManager<TransactionalMemory.Transaction> {
    public STMContextManager(TransactionalMemory.Transaction tx, Context context) {
      super("tx", context.lookupGlobalFunction("STM", "run"), tx);
    }
  }

  @NodeInfo(shortName = "new")
  abstract static class STMBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public TransactionalMemory stm() {
      return new TransactionalMemory();
    }
  }

  @NodeInfo(shortName = "var")
  abstract static class VarBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public TransactionalMemory.Var var(TransactionalMemory memory, Object initial) {
      return new TransactionalMemory.Var(memory, initial);
    }
  }

  private static TransactionalMemory.Transaction lookupTx(Context context, Node node) {
    STMContextManager txNative = (STMContextManager) context.lookupLocalContext(TX_CONTEXT_NAME);
    return txNative.nativeData(node);
  }

  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(STMContextManager contextManager, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YonaLanguage.class) Context context) {
      Object result;
      while (true) {
        final TransactionalMemory.Transaction tx = lookupTx(context, this);
        try {
          result = tryExecuteTransaction(function, tx, dispatch);
          if (!(result instanceof Promise)) {
            if (tx.validate()) {
              tx.commit();
              break;
            } else {
              tx.abort();
              tx.reset();
            }
          } else {
            break;
          }
        } catch (Throwable t) {
          tx.abort();
          throw t;
        }
      }

      return result;
    }

    @CompilerDirectives.TruffleBoundary
    private Object tryExecuteTransaction(final Function function, final TransactionalMemory.Transaction tx, final InteropLibrary dispatch) {
      Object result;
      try {
        tx.start();
        result = dispatch.execute(function);
        if (result instanceof Promise) {
          Promise resultPromise = (Promise) result;
          result = resultPromise.map(value -> {
            if (tx.validate()) {
              tx.commit();
              return value;
            } else {
              tx.abort();
              tx.reset();
              try {
                return tryExecuteTransaction(function, tx, dispatch);
              } catch (Throwable e) {
                tx.abort();
                e.printStackTrace();
                throw e;
              }
            }
          }, exception -> {
            tx.abort();
            return exception;
          }, this);
        }
      } catch (UnsupportedTypeException | ArityException | UnsupportedMessageException e) {
        tx.abort();
        throw new STMException(e, this);
      } catch (YonaException e) {
        tx.abort();
        throw e;
      } catch (Throwable e) {
        tx.abort();
        throw e;
      }

      return result;
    }
  }

  @NodeInfo(shortName = "read_tx")
  abstract static class ReadTxBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Tuple readTx(TransactionalMemory stm, @CachedContext(YonaLanguage.class) Context context) {
      return new STMContextManager(new TransactionalMemory.ReadOnlyTransaction(stm), context);
    }
  }

  @NodeInfo(shortName = "write_tx")
  abstract static class WriteTxBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Tuple writeTx(TransactionalMemory stm, @CachedContext(YonaLanguage.class) Context context) {
      return new STMContextManager(new TransactionalMemory.ReadWriteTransaction(stm), context);
    }
  }

  @NodeInfo(shortName = "read")
  abstract static class ReadBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object read(TransactionalMemory.Var var, @CachedContext(YonaLanguage.class) Context context) {
      if (!context.containsLocalContext(TX_CONTEXT_NAME)) {
        return var.read();
      } else {
        final TransactionalMemory.Transaction tx = lookupTx(context, this);
        return var.read(tx, this);
      }
    }
  }

  @NodeInfo(shortName = "write")
  abstract static class WriteBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit write(TransactionalMemory.Var var, Object value, @CachedContext(YonaLanguage.class) Context context) {
      final TransactionalMemory.Transaction tx = lookupTx(context, this);
      if (tx == null) {
        throw new STMException("There is no running STM transaction", this);
      }
      var.write(tx, value, this);
      return Unit.INSTANCE;
    }
  }

  @NodeInfo(shortName = "protect")
  abstract static class ProtectBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit protect(TransactionalMemory.Var var, @CachedContext(YonaLanguage.class) Context context) {
      final TransactionalMemory.Transaction tx = lookupTx(context, this);
      if (tx == null) {
        throw new STMException("There is no running STM transaction", this);
      }
      var.protect(tx, this);
      return Unit.INSTANCE;
    }
  }

  @Override
  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.STMBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.VarBuiltinFactory.getInstance()));
    builtins.register(new PrivateFunction(STMBuiltinModuleFactory.RunBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ReadTxBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.WriteTxBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ReadBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.WriteBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ProtectBuiltinFactory.getInstance()));
    return builtins;
  }
}
