package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.runtime.Tuple;
import yatta.runtime.YattaModule;

import java.util.Arrays;
import java.util.Objects;

import static com.oracle.truffle.api.CompilerDirectives.CompilationFinal;

public final class RecordTypePatternNode extends MatchNode {
  @CompilationFinal private final String recordType;
  @Children private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public RecordTypePatternNode(String recordType, ExpressionNode[] moduleStack) {
    this.recordType = recordType;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    RecordTypePatternNode that = (RecordTypePatternNode) o;
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
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Tuple) {
      if (moduleStack.length > 0) {
        for (int i = moduleStack.length - 1; i >= 0; i--) {
          try {
            YattaModule module = moduleStack[i].executeModule(frame);
            if (module.getRecords().contains(recordType)) {
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
