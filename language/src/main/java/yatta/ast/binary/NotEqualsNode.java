package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.runtime.*;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "!=")
public abstract class NotEqualsNode extends BinaryOpNode {
  @Specialization
  public boolean longs(long left, long right) {
    return left != right;
  }

  @Specialization
  public boolean doubles(double left, double right) {
    return left != right;
  }

  @Specialization
  public boolean bytes(byte left, byte right) {
    return left != right;
  }

  @Specialization
  public boolean functions(Function left, Function right) {
    return left != right;
  }

  @Specialization
  public boolean units(Unit left, Unit right) {
    return false;
  }

  @Specialization
  public boolean tuples(Tuple left, Tuple right) {
    return !left.equals(right);
  }

  @Specialization
  public boolean modules(Module left, Module right) {
    return left != right;
  }

  @Specialization
  public boolean stringLists(StringList left, StringList right) {
    return !left.equals(right);
  }

  @Specialization
  public boolean sequences(Seq left, Seq right) {
    return !left.equals(right);
  }

  @Specialization
  public boolean dictionaries(Dict left, Dict right) {
    return !left.equals(right);
  }

  @Specialization
  public boolean set(Set left, Set right) {
    return !left.equals(right);
  }

  @Specialization
  public boolean nativeObjects(NativeObject left, NativeObject right) {
    return !left.equals(right);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[] {left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;
      return !argValues[0].equals(argValues[1]);
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
    return !left.equals(right);
  }
}
