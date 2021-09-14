package yona.runtime;

import com.oracle.truffle.api.nodes.Node;

public abstract class NativeObjectContextManager<T> extends ContextManager<NativeObject<T>> {
  public NativeObjectContextManager(String contextIdentifier, Function wrapperFunction, T data) {
    super(contextIdentifier, wrapperFunction, new NativeObject<>(data));
  }

  public NativeObjectContextManager(Seq contextIdentifier, Function wrapperFunction, T data) {
    super(contextIdentifier, wrapperFunction, new NativeObject<>(data));
  }

  public T nativeData(Node node) {
    return (T) getData(NativeObject.class, node).getValue();
  }
}
