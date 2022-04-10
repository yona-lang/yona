package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.ExpressionNode;
import yona.ast.builtin.BuiltinNode;
import yona.ast.call.InvokeNode;
import yona.ast.expression.value.StringNode;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.async.TransactionalMemory;
import yona.runtime.exceptions.STMException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;
import yona.runtime.stdlib.PrivateFunction;

import java.util.Objects;

@BuiltinModuleInfo(moduleName = "STM")
public class STMBuiltinModule implements BuiltinModule {
  private static final String TX_CONTEXT_NAME = "$tx";
  private static final Seq TX_CONTEXT_NAME_SEQ = Seq.fromCharSequence(TX_CONTEXT_NAME);

  protected static final class STMContextManager extends NativeObjectContextManager<TransactionalMemory.Transaction> {
    public STMContextManager(TransactionalMemory.Transaction tx, Context context) {
      super(TX_CONTEXT_NAME_SEQ, context.lookupGlobalFunction("STM", "run"), tx);
    }

    public static STMContextManager adapt(ContextManager<?> contextManager, Context context, Node node) {
      return new STMContextManager(((NativeObject<TransactionalMemory.Transaction>) Objects.requireNonNull(contextManager).getData(NativeObject.class, node)).getValue(), context);
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

  private static TransactionalMemory.Transaction lookupTx(VirtualFrame frame, Context context, Node node) {
    Object contextManager = LocalContextBuiltinModuleFactory.LookupBuiltinFactory.create(new ExpressionNode[]{new StringNode(TX_CONTEXT_NAME_SEQ)}).executeGeneric(frame);
    if (contextManager == UninitializedFrameSlot.INSTANCE) {
      throw new STMException("There is no running STM transaction", node);
    }
    STMContextManager txNative = STMContextManager.adapt((ContextManager<?>) contextManager, context, node);
    return txNative.nativeData(node);
  }

  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    public Object run(ContextManager<?> contextManager, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      STMContextManager txNative = STMContextManager.adapt(contextManager, Context.get(this), this);
      Object result;

      while (true) {
        final TransactionalMemory.Transaction tx;
        tx = txNative.nativeData(this);
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
        result = InvokeNode.dispatchFunction(function, dispatch, this);
        if (result instanceof Promise resultPromise) {
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
    public Tuple readTx(TransactionalMemory stm) {
      return new STMContextManager(new TransactionalMemory.ReadOnlyTransaction(stm), Context.get(this));
    }
  }

  @NodeInfo(shortName = "write_tx")
  abstract static class WriteTxBuiltin extends BuiltinNode {
    @Specialization
    public Tuple writeTx(TransactionalMemory stm) {
      return new STMContextManager(new TransactionalMemory.ReadWriteTransaction(stm), Context.get(this));
    }
  }

  @NodeInfo(shortName = "read")
  abstract static class ReadBuiltin extends BuiltinNode {
    @Specialization
    public Object read(VirtualFrame frame, TransactionalMemory.Var var) {
      Object contextManager = LocalContextBuiltinModuleFactory.LookupBuiltinFactory.create(new ExpressionNode[]{new StringNode(TX_CONTEXT_NAME_SEQ)}).executeGeneric(frame);
      if (contextManager != UninitializedFrameSlot.INSTANCE) {
        STMContextManager stmContextManager = STMContextManager.adapt((ContextManager<?>) contextManager, Context.get(this), this);
        final TransactionalMemory.Transaction tx = stmContextManager.nativeData(this);
        return var.read(tx, this);
      }

      return var.read();
    }
  }

  @NodeInfo(shortName = "write")
  abstract static class WriteBuiltin extends BuiltinNode {
    @Specialization
    public Unit write(VirtualFrame frame, TransactionalMemory.Var var, Object value) {
      final TransactionalMemory.Transaction tx = lookupTx(frame, Context.get(this), this);
      var.write(tx, value, this);
      return Unit.INSTANCE;
    }
  }

  @NodeInfo(shortName = "protect")
  abstract static class ProtectBuiltin extends BuiltinNode {
    @Specialization
    public Unit protect(VirtualFrame frame, TransactionalMemory.Var var) {
      final TransactionalMemory.Transaction tx = lookupTx(frame, Context.get(this), this);
      var.protect(tx, this);
      return Unit.INSTANCE;
    }
  }

  @Override
  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(STMBuiltinModuleFactory.STMBuiltinFactory.getInstance()),
        new ExportedFunction(STMBuiltinModuleFactory.VarBuiltinFactory.getInstance()),
        new PrivateFunction(STMBuiltinModuleFactory.RunBuiltinFactory.getInstance()),
        new ExportedFunction(STMBuiltinModuleFactory.ReadTxBuiltinFactory.getInstance()),
        new ExportedFunction(STMBuiltinModuleFactory.WriteTxBuiltinFactory.getInstance()),
        new ExportedFunction(STMBuiltinModuleFactory.ReadBuiltinFactory.getInstance()),
        new ExportedFunction(STMBuiltinModuleFactory.WriteBuiltinFactory.getInstance()),
        new ExportedFunction(STMBuiltinModuleFactory.ProtectBuiltinFactory.getInstance())
    );
  }
}
