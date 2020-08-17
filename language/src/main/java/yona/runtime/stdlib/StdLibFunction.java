package yona.runtime.stdlib;

import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Context;

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
    Source source = Source.newBuilder(YonaLanguage.ID, "", Context.lookupNodeInfo(node.getNodeClass()).shortName()).internal(true).build();
    return source.createUnavailableSection();
  }
}
