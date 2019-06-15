package abzu.ast.binary;

import abzu.runtime.*;
import abzu.runtime.async.Promise;
import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo
public abstract class EqualsNode extends BinaryOpNode {
  @Specialization
  public boolean longs(long left, long right) {
    return left == right;
  }

  @Specialization
  public boolean doubles(double left, double right) {
    return left == right;
  }

  @Specialization
  public boolean bytes(byte left, byte right) {
    return left == right;
  }

  @Specialization
  public boolean strings(String left, String right) {
    return left.equals(right);
  }

  @Specialization
  public boolean functions(Function left, Function right) {
    return left == right;
  }

  @Specialization
  public boolean units(Unit left, Unit right) {
    return true;
  }

  @Specialization
  public boolean tuples(Tuple left, Tuple right) {
    return left.equals(right);
  }

  @Specialization
  public boolean modules(Module left, Module right) {
    return left == right;
  }

  @Specialization
  public boolean stringLists(StringList left, StringList right) {
    return left.equals(right);
  }

  @Specialization
  public boolean sequences(Sequence left, Sequence right) {
    return left.equals(right);
  }

  @Specialization
  public boolean dictionaries(Dictionary left, Dictionary right) {
    return left.equals(right);
  }

  @Specialization
  public boolean nativeObjects(NativeObject left, NativeObject right) {
    return left.equals(right);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[] {left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;
      return argValues[0].equals(argValues[1]);
    }, this);
  }

  @Specialization
  public Promise leftPromise(Promise left, Object right) {
    return promise(left, right);
  }

  @Specialization
  public Promise rightPromise(Object left, Promise right) {
    return promise(left, right);
  }

  @Fallback
  public boolean fallback(Object left, Object right) {
    return left.equals(right);
  }
}
