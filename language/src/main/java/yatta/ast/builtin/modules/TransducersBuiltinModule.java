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
import yatta.runtime.Dict;
import yatta.runtime.Function;
import yatta.runtime.Tuple;
import yatta.runtime.Unit;
import yatta.runtime.exceptions.TransducerDoneException;
import yatta.runtime.exceptions.UndefinedNameException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;
import yatta.runtime.stdlib.PrivateFunction;

@BuiltinModuleInfo(moduleName = "Transducers")
public final class TransducersBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "raise_done")
  abstract static class RaiseDoneBuiltin extends BuiltinNode {
    @Specialization
    public Object raiseDone() {
      throw TransducerDoneException.INSTANCE;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new PrivateFunction(TransducersBuiltinModuleFactory.RaiseDoneBuiltinFactory.getInstance()));
    return builtins;
  }
}
