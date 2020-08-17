package yona.runtime.threading;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Dict;
import yona.runtime.Function;
import yona.runtime.async.Promise;

public final class Task {
  Promise promise;
  Function function;
  InteropLibrary dispatch;
  Node node;
  Dict localContexts;
}
