package yatta.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.*;
import yatta.runtime.async.TransactionalMemory;
import yatta.runtime.exceptions.STMException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "STM")
public class STMBuiltinModule implements BuiltinModule {
  private static final String TX_CONTEXT_NAME = "tx";

  private static final TruffleLogger LOGGER = YattaLanguage.getLogger(STMBuiltinModule.class);

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

  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    public Object run(Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YattaLanguage.class) Context context) {
      System.out.println(context.containsLocalContext(TX_CONTEXT_NAME));
      final TransactionalMemory.Transaction tx = (TransactionalMemory.Transaction) context.lookupLocalContext(TX_CONTEXT_NAME);
      Object result;
      while (true) {
        try {
          tx.start();
          System.out.println(tx);
          result = dispatch.execute(function);
          if (tx.validate()) {
            tx.commit();
            break;
          } else {
            tx.abort();
            tx.reset();
          }
        } catch (UnsupportedTypeException | ArityException | UnsupportedMessageException e) {
          tx.abort();
          throw new YattaException(e, this);
        } catch (Exception e) {
          tx.abort();
          throw e;
        }
      }
      return result;
    }
  }

  @NodeInfo(shortName = "read_tx")
  abstract static class ReadTxBuiltin extends BuiltinNode {
    @Specialization
    public Tuple readTx(TransactionalMemory stm) {
      return new Tuple(Seq.fromCharSequence(TX_CONTEXT_NAME), new TransactionalMemory.ReadOnlyTransaction(stm));
    }
  }

  @NodeInfo(shortName = "write_tx")
  abstract static class WriteTxBuiltin extends BuiltinNode {
    @Specialization
    public Tuple writeTx(TransactionalMemory stm) {
      return new Tuple(Seq.fromCharSequence(TX_CONTEXT_NAME), new TransactionalMemory.ReadWriteTransaction(stm));
    }
  }

  @NodeInfo(shortName = "read")
  abstract static class ReadBuiltin extends BuiltinNode {
    @Specialization
    public Object read(TransactionalMemory.Var var, @CachedContext(YattaLanguage.class) Context context) {
      if (!context.containsLocalContext(TX_CONTEXT_NAME)) {
        return var.read();
      } else {
        TransactionalMemory.Transaction tx = (TransactionalMemory.Transaction) context.lookupLocalContext(TX_CONTEXT_NAME);
        return var.read(tx, this);
      }
    }
  }

  @NodeInfo(shortName = "write")
  abstract static class WriteBuiltin extends BuiltinNode {
    @Specialization
    public Unit write(TransactionalMemory.Var var, Object value, @CachedContext(YattaLanguage.class) Context context) {
      TransactionalMemory.Transaction tx = (TransactionalMemory.Transaction) context.lookupLocalContext(TX_CONTEXT_NAME);
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
    public Unit protect(TransactionalMemory.Var var, @CachedContext(YattaLanguage.class) Context context) {
      TransactionalMemory.Transaction tx = (TransactionalMemory.Transaction) context.lookupLocalContext(TX_CONTEXT_NAME);
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
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.RunBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ReadTxBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.WriteTxBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ReadBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.WriteBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ProtectBuiltinFactory.getInstance()));
    return builtins;
  }
}
