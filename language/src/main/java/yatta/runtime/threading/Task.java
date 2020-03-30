package yatta.runtime.threading;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.Function;
import yatta.runtime.async.Promise;
import yatta.runtime.async.TransactionalMemory;

public final class Task {
  Promise promise;
  Function function;
  InteropLibrary dispatch;
  Node node;
  TransactionalMemory.Transaction tx;
}
