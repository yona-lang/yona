package yona.ast.builtin.modules.socket;

import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Context;
import yona.runtime.ContextManager;
import yona.runtime.NativeObject;
import yona.runtime.NativeObjectContextManager;
import yona.runtime.network.YonaConnection;

final class ConnectionContextManager extends NativeObjectContextManager<YonaConnection> {
  public ConnectionContextManager(YonaConnection yonaConnection, Context context) {
    super("connection", context.lookupGlobalFunction("socket\\Connection", "run"), yonaConnection);
  }

  public static ConnectionContextManager adapt(ContextManager<?> contextManager, Context context, Node node) {
    return new ConnectionContextManager(((NativeObject<YonaConnection>) contextManager.getData(node)).getValue(), context);
  }
}
