package abzu.ast.builtin;

import abzu.runtime.Function;
import abzu.runtime.Sequence;
import abzu.runtime.UndefinedNameException;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "sfoldr")
public abstract class SequenceFoldRightBuiltin extends BuiltinNode {
  @Specialization
  public Object foldRight(Sequence sequence, Function function, Object initialValue, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    return sequence.foldRight((acc, val) -> {
      try {
        return dispatch.execute(function, new Object[] {acc, val});
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw UndefinedNameException.undefinedFunction(this, function);
      }
    }, initialValue);
  }
}
