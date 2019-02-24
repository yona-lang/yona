package abzu.ast.builtin;

import abzu.ast.call.DispatchNode;
import abzu.ast.call.DispatchNodeGen;
import abzu.runtime.Function;
import abzu.runtime.Sequence;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "sfoldl")
public abstract class SequenceFoldLeftBuiltin extends BuiltinNode {
  @Specialization
  public Object foldLeft(Sequence sequence, Function function, Object initialValue) {
    return sequence.foldLeft((acc, val) -> {
      DispatchNode dispatchNode = DispatchNodeGen.create();
      return dispatchNode.executeDispatch(function, new Object[] {acc, val});
    }, initialValue);
  }
}
