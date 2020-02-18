package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.AliasNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.AnyValueNode;
import yatta.runtime.Tuple;
import yatta.runtime.YattaModule;

import java.util.Arrays;
import java.util.Objects;

import static com.oracle.truffle.api.CompilerDirectives.CompilationFinal;

public final class AsRecordMatchNode extends MatchNode {
  @CompilationFinal private final String recordType;
  @Child public IdentifierNode identifierNode;
  @Children private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public AsRecordMatchNode(String recordType, IdentifierNode identifierNode, ExpressionNode[] moduleStack) {
    this.recordType = recordType;
    this.identifierNode = identifierNode;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AsRecordMatchNode that = (AsRecordMatchNode) o;
    return Objects.equals(recordType, that.recordType) &&
        Objects.equals(identifierNode, that.identifierNode) &&
        Arrays.equals(moduleStack, that.moduleStack);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(recordType, identifierNode);
    result = 31 * result + Arrays.hashCode(moduleStack);
    return result;
  }

  @Override
  public String toString() {
    return "AsRecordMatchNode{" +
        "recordType='" + recordType + '\'' +
        ", identifierNode=" + identifierNode +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Tuple) {
      Tuple tuple = (Tuple) value;

      if (moduleStack.length > 0) {
        for (int i = moduleStack.length - 1; i >= 0; i--) {
          try {
            YattaModule module = moduleStack[i].executeModule(frame);
            if (module.getRecords().contains(recordType)) {
              AliasNode identifierAlias = new AliasNode(identifierNode.name(), new AnyValueNode(tuple));
              identifierAlias.executeGeneric(frame);
              return MatchResult.TRUE;
            }
          } catch (UnexpectedResultException e) {
            continue;
          } catch (YattaException e) {  // IO error
            continue;
          }
        }
      }
    }

    return MatchResult.FALSE;
  }
}
