package yona.ast.builtin.modules.socket;

import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Context;
import yona.runtime.ContextManager;
import yona.runtime.NativeObject;
import yona.runtime.NativeObjectContextManager;
import yona.runtime.network.TCPConnection;

final class ConnectionContextManager extends NativeObjectContextManager<TCPConnection> {
  public ConnectionContextManager(TCPConnection TCPConnection, Context context) {
    super("connection", context.lookupGlobalFunction("socket\\tcp\\Connection", "run"), TCPConnection);
  }

  public static ConnectionContextManager adapt(ContextManager<?> contextManager, Context context, Node node) {
    return new ConnectionContextManager(((NativeObject<TCPConnection>) contextManager.getData(node)).getValue(), context);
  }
}
