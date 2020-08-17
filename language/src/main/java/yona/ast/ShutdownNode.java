package yona.ast;

import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import yona.YonaLanguage;
import yona.runtime.Unit;

@NodeInfo(language = "yona", description = "Shutdown node")
public class ShutdownNode extends RootNode {
  public ShutdownNode(TruffleLanguage<?> language) {
    super(language);
  }

  @Override
  public Object execute(VirtualFrame frame) {
    lookupContextReference(YonaLanguage.class).get().dispose();
    return Unit.INSTANCE;
  }
}
