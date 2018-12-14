package abzu.ast.expression.value;

import abzu.AbzuException;
import abzu.AbzuLanguage;
import abzu.ast.ExpressionNode;
import abzu.ast.expression.SimpleIdentifierNode;
import abzu.runtime.StringList;
import abzu.runtime.Module;
import abzu.runtime.Tuple;
import com.oracle.truffle.api.CallTarget;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.profiles.BranchProfile;
import com.oracle.truffle.api.profiles.ConditionProfile;
import com.oracle.truffle.api.source.Source;

import java.io.IOException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class FQNNode extends ExpressionNode {
  private final AbzuLanguage abzuLanguage;
  public final String[] parts;

  public FQNNode(AbzuLanguage abzuLanguage, String[] parts) {
    this.abzuLanguage = abzuLanguage;
    this.parts = parts;
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
    return lookupModule();
  }

  @Override
  public Tuple executeTuple(VirtualFrame frame) throws UnexpectedResultException {
    return new Tuple(parts);
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
    return lookupModule();
  }

  private Module lookupModule() {
    try {
      Path path;
      if (parts.length >= 2) {
        String[] pathParts = new String[parts.length - 1];
        System.arraycopy(parts, 1, pathParts, 0, parts.length - 1);
        pathParts[parts.length - 2] = pathParts[parts.length - 2] + "." + AbzuLanguage.ID;
        path = Paths.get(parts[0], pathParts);
      } else {
        path = Paths.get(parts[0]);
      }
      URL url = path.toUri().toURL();

      Source source = Source.newBuilder(AbzuLanguage.ID, url).build();
      CallTarget callTarget = abzuLanguage.getContextReference().get().parse(source);
      Module module = (Module) callTarget.call();

      if (!Arrays.equals(module.getFqn().toArray(), parts)) {
        throw new AbzuException("Module file " + url.getPath().substring(Paths.get(".").toUri().toURL().getFile().length() - 2) + " has incorrectly defined module as " + module.getFqn(), this);
      }

      return module;
    } catch (IOException e) {
      throw new AbzuException("Unable to load Module " + Arrays.toString(parts) + " due to: " + e.getMessage(), this);
    }
  }
}
