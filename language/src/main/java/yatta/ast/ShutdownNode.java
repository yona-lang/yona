package yatta.ast;

import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import yatta.YattaLanguage;
import yatta.runtime.Unit;

@NodeInfo(language = "yatta", description = "Shutdown node")
public class ShutdownNode extends RootNode {
  public ShutdownNode(TruffleLanguage<?> language) {
    super(language);
  }

  @Override
  public Object execute(VirtualFrame frame) {
    lookupContextReference(YattaLanguage.class).get().dispose();
    return Unit.INSTANCE;
  }
}
