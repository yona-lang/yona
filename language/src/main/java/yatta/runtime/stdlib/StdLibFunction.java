package yatta.runtime.stdlib;

import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Context;

public abstract class StdLibFunction {
  public final NodeFactory<? extends BuiltinNode> node;

  public StdLibFunction(NodeFactory<? extends BuiltinNode> node) {
    this.node = node;
  }

  public abstract boolean isExported();

  public boolean unwrapArgumentPromises() {
    return true;
  }

  public SourceSection sourceSection() {
    Source source = Source.newBuilder(YattaLanguage.ID, "", Context.lookupNodeInfo(node.getNodeClass()).shortName()).internal(true).build();
    return source.createUnavailableSection();
  }
}
