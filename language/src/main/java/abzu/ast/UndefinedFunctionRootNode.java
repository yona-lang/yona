package abzu.ast;

import abzu.AbzuLanguage;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.RootNode;
import abzu.runtime.AbzuUndefinedNameException;

/**
 * The initial {@link RootNode} of {@link abzu.runtime.Function functions} when they are created, i.e., when
 * they are still undefined. Executing it throws an
 * {@link AbzuUndefinedNameException#undefinedFunction exception}.
 */
public class UndefinedFunctionRootNode extends AbzuRootNode {
  public UndefinedFunctionRootNode(AbzuLanguage language, String name) {
    super(language, null, null, null, name);
  }

  @Override
  public Object execute(VirtualFrame frame) {
    throw AbzuUndefinedNameException.undefinedFunction(null, getName());
  }
}
