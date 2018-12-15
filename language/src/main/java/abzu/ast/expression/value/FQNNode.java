package abzu.ast.expression.value;

import abzu.AbzuLanguage;
import abzu.ast.ExpressionNode;
import abzu.ast.call.ModuleCacheNode;
import abzu.ast.call.ModuleCacheNodeGen;
import abzu.ast.expression.SimpleIdentifierNode;
import abzu.runtime.Module;
import abzu.runtime.StringList;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.profiles.BranchProfile;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class FQNNode extends ExpressionNode {
  @Child private ModuleCacheNode moduleCacheNode;
  private final String[] parts;

  public FQNNode(String[] parts) {
    this.parts = parts;
    this.moduleCacheNode = ModuleCacheNodeGen.create();
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FQNNode fqnNode = (FQNNode) o;
    return Objects.equals(parts, fqnNode.parts);
  }

  @Override
  public int hashCode() {
    return Objects.hash(parts);
  }

  @Override
  public String toString() {
    return "FQNNode{" +
           "strings=" + Arrays.toString(parts) +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return moduleCacheNode.executeLoad(parts);
  }

  @Override
  public StringList executeStringList(VirtualFrame frame) throws UnexpectedResultException {
    return new StringList(parts);
  }

  private final BranchProfile branchProfile = BranchProfile.create();

  @Override
  public Module executeModule(VirtualFrame frame) throws UnexpectedResultException {
    if (parts.length == 1) {
      try {
        SimpleIdentifierNode simpleIdentifierNode = new SimpleIdentifierNode(parts[0]);
        branchProfile.enter();
        return simpleIdentifierNode.executeModule(frame);
      } catch(UnexpectedResultException ex) {
        // no-action, read module as usual
      }
    }
    branchProfile.enter();
    return moduleCacheNode.executeLoad(parts);
  }
}
