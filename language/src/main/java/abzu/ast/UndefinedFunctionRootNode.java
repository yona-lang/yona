package abzu.ast;

import abzu.AbzuLanguage;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.RootNode;
import abzu.runtime.UndefinedNameException;

/**
 * The initial {@link RootNode} of {@link abzu.runtime.Function functions} when they are created, i.e., when
 * they are still undefined. Executing it throws an
 * {@link UndefinedNameException#undefinedFunction exception}.
 */
public class UndefinedFunctionRootNode extends AbzuRootNode {
  public UndefinedFunctionRootNode(AbzuLanguage language, String name) {
    super(language, null, null, null, name, null);
  }

  @Override
  public Object execute(VirtualFrame frame) {
    throw UndefinedNameException.undefinedFunction(null, getName());
  }
}
