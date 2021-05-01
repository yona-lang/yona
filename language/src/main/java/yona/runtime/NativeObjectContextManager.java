package yona.runtime;

import com.oracle.truffle.api.nodes.Node;

public abstract class NativeObjectContextManager<T> extends ContextManager<NativeObject<T>> {
  public NativeObjectContextManager(String contextIdentifier, Function wrapperFunction, T data) {
    super(contextIdentifier, wrapperFunction, new NativeObject<T>(data));
  }

  public T nativeData(Node node) {
    return getData(node).getValue();
  }
}
