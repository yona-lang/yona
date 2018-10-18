package abzu.ast;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.RootNode;
import abzu.AbzuLanguage;
import abzu.runtime.AbzuUndefinedNameException;

/**
 * The initial {@link RootNode} of {@link AbzuFunction functions} when they are created, i.e., when
 * they are still undefined. Executing it throws an
 * {@link AbzuUndefinedNameException#undefinedFunction exception}.
 */
public class AbzuUndefinedFunctionRootNode extends AbzuRootNode {
  public AbzuUndefinedFunctionRootNode(AbzuLanguage language, String name) {
    super(language, null, null, null, name);
  }

  @Override
  public Object execute(VirtualFrame frame) {
    throw AbzuUndefinedNameException.undefinedFunction(null, getName());
  }
}
