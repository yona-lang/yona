package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Function;
import yatta.runtime.Unit;
import yatta.runtime.async.TransactionalMemory;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;
import yatta.runtime.threading.Threading;

@BuiltinModuleInfo(moduleName = "STM")
public class STMBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "new")
  abstract static class STMBuiltin extends BuiltinNode {
    @Specialization
    public TransactionalMemory stm() {
      return new TransactionalMemory();
    }
  }

  @NodeInfo(shortName = "var")
  abstract static class VarBuiltin extends BuiltinNode {
    @Specialization
    public TransactionalMemory.Var var(TransactionalMemory memory, Object initial) {
      return new TransactionalMemory.Var(memory, initial);
    }
  }

  @NodeInfo(shortName = "transaction")
  abstract static class TransactionBuiltin extends BuiltinNode {
    @Specialization
    public Object transaction(TransactionalMemory stm, boolean readOnly, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (Threading.TX.get() != null) {
        throw new YattaException("STM transaction is already running", this);
      }
      final TransactionalMemory.Transaction tx;
      if (readOnly) {
        tx = new TransactionalMemory.ReadOnlyTransaction(stm);
      } else {
        tx = new TransactionalMemory.ReadWriteTransaction(stm);
      }
      Threading.TX.set(tx);
      try {
        Object result;
        while (true) {
          try {
            tx.start();
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
      } finally {
        Threading.TX.remove();
      }
    }
  }

  @NodeInfo(shortName = "read")
  abstract static class ReadBuiltin extends BuiltinNode {
    @Specialization
    public Object read(TransactionalMemory.Var var) {
      TransactionalMemory.Transaction tx = Threading.TX.get();
      if (tx == null) {
        return var.read();
      } else {
        return var.read(tx, this);
      }
    }
  }

  @NodeInfo(shortName = "write")
  abstract static class WriteBuiltin extends BuiltinNode {
    @Specialization
    public Unit write(TransactionalMemory.Var var, Object value) {
      TransactionalMemory.Transaction tx = Threading.TX.get();
      if (tx == null) {
        throw new YattaException("There is no running STM transaction", this);
      }
      var.write(tx, value, this);
      return Unit.INSTANCE;
    }
  }

  @NodeInfo(shortName = "protect")
  abstract static class ProtectBuiltin extends BuiltinNode {
    @Specialization
    public Unit protect(TransactionalMemory.Var var) {
      TransactionalMemory.Transaction tx = Threading.TX.get();
      if (tx == null) {
        throw new YattaException("There is no running STM transaction", this);
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
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.TransactionBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ReadBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.WriteBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(STMBuiltinModuleFactory.ProtectBuiltinFactory.getInstance()));
    return builtins;
  }
}
