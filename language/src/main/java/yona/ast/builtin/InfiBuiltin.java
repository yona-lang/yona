package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.runtime.Function;
import yona.runtime.exceptions.BadArgException;

@NodeInfo(shortName = "infi")
public abstract class InfiBuiltin extends AbstractLoopNode {
  @Specialization
  public Object infi(Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    if (function.getCardinality() > 0) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("infi function accepts only functions with zero arguments. Function " + function + " expects " + function.getCardinality() + "arguments", this);
    }

    while (true) {
      runCallback(function, dispatch);
    }
  }
}

