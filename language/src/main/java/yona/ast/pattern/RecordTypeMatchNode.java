package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.YonaException;
import yona.ast.ExpressionNode;
import yona.runtime.Tuple;
import yona.runtime.YonaModule;

import java.util.Arrays;
import java.util.Objects;

import static com.oracle.truffle.api.CompilerDirectives.CompilationFinal;

@NodeInfo(shortName = "recordTypeMatch")
public final class RecordTypeMatchNode extends MatchNode {
  @CompilationFinal private final String recordType;
  @Children private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public RecordTypeMatchNode(String recordType, ExpressionNode[] moduleStack) {
    this.recordType = recordType;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    RecordTypeMatchNode that = (RecordTypeMatchNode) o;
    return Objects.equals(recordType, that.recordType) &&
        Arrays.equals(moduleStack, that.moduleStack);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(recordType);
    result = 31 * result + Arrays.hashCode(moduleStack);
    return result;
  }

  @Override
  public String toString() {
    return "RecordTypeNode{" +
        "recordType='" + recordType + '\'' +
        '}';
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Tuple) {
      if (moduleStack.length > 0) {
        for (int i = moduleStack.length - 1; i >= 0; i--) {
          try {
            YonaModule module = moduleStack[i].executeModule(frame);
            if (module.getRecords().contains(recordType)) {
              return MatchResult.TRUE;
            }
          } catch (UnexpectedResultException e) {
            continue;
          } catch (YonaException e) {  // IO error
            continue;
          }
        }
      }
    }

    return MatchResult.FALSE;
  }

  @Override
  protected String[] providedIdentifiers() {
    return new String[0];
  }
}
