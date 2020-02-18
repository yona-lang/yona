package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.AliasNode;
import yatta.runtime.Context;
import yatta.runtime.Symbol;
import yatta.runtime.Tuple;
import yatta.runtime.YattaModule;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

import static com.oracle.truffle.api.CompilerDirectives.CompilationFinal;

public final class RecordPatternNode extends MatchNode {
  @CompilationFinal private final String recordType;
  @Children public RecordPatternFieldNode[] fieldMatchNodes;
  @Children private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public RecordPatternNode(String recordType, RecordPatternFieldNode[] fieldMatchNodes, ExpressionNode[] moduleStack) {
    this.recordType = recordType;
    this.fieldMatchNodes = fieldMatchNodes;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    RecordPatternNode that = (RecordPatternNode) o;
    return Objects.equals(recordType, that.recordType) &&
        Arrays.equals(fieldMatchNodes, that.fieldMatchNodes) &&
        Arrays.equals(moduleStack, that.moduleStack);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(recordType);
    result = 31 * result + Arrays.hashCode(fieldMatchNodes);
    result = 31 * result + Arrays.hashCode(moduleStack);
    return result;
  }

  @Override
  public String toString() {
    return "RecordPatternNode{" +
        "recordType='" + recordType + '\'' +
        ", fieldMatchNodes=" + Arrays.toString(fieldMatchNodes) +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Tuple) {
      Tuple tuple = (Tuple) value;

      String[] recordFields = null;
      if (moduleStack.length > 0) {
        for (int i = moduleStack.length - 1; i >= 0; i--) {
          try {
            YattaModule module = moduleStack[i].executeModule(frame);
            if (module.getRecords().contains(recordType)) {
              recordFields = (String[]) module.getRecords().lookup(recordType);
            }
          } catch (UnexpectedResultException e) {
            continue;
          } catch (YattaException e) {  // IO error
            continue;
          }
        }
      }

      if (recordFields != null) {
        Context context = lookupContextReference(YattaLanguage.class).get();
        Symbol recordTypeSymbol = context.symbol(recordType);

        if (tuple.get(0) instanceof Symbol && recordTypeSymbol.equals((tuple.get(0))) && recordFields.length + 1 == tuple.length()) {
          List<AliasNode> aliases = new ArrayList<>();

          boolean matched = false;
          for (RecordPatternFieldNode fieldMatchNode : fieldMatchNodes) {
            MatchResult matchResult = fieldMatchNode.match(new Object[]{tuple, recordFields}, frame);
            if (!matchResult.isMatches()) {
              continue;
            } else {
              matched = true;
              aliases.addAll(Arrays.asList(matchResult.getAliases()));
            }
          }

          if (matched) {
            for (AliasNode aliasNode : aliases) {
              aliasNode.executeGeneric(frame);
            }

            return MatchResult.TRUE;
          }
        }
      }
    }

    return MatchResult.FALSE;
  }

  public static final class RecordPatternFieldNode extends MatchNode {
    @CompilationFinal String fieldName;
    @Child MatchNode fieldValue;

    public RecordPatternFieldNode(String fieldName, MatchNode fieldValue) {
      this.fieldName = fieldName;
      this.fieldValue = fieldValue;
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) return true;
      if (o == null || getClass() != o.getClass()) return false;
      RecordPatternFieldNode that = (RecordPatternFieldNode) o;
      return Objects.equals(fieldName, that.fieldName) &&
          Objects.equals(fieldValue, that.fieldValue);
    }

    @Override
    public int hashCode() {
      return Objects.hash(fieldName, fieldValue);
    }

    @Override
    public String toString() {
      return "RecordPatternFieldNode{" +
          "fieldName=" + fieldName +
          ", fieldValue=" + fieldValue +
          '}';
    }

    @Override
    public MatchResult match(Object value, VirtualFrame frame) {
      Object[] inputValues = (Object[]) value;
      Tuple tuple = (Tuple) inputValues[0];
      String[] recordFields = (String[]) inputValues[1];

      int fieldPos = -1;
      for (int i = 0; i < recordFields.length; i++) {
        if (fieldName.equals(recordFields[i])) {
          fieldPos = i + 1;
          break;
        }
      }

      if (fieldPos != -1) {
        return fieldValue.match(tuple.get(fieldPos), frame);
      }

      return MatchResult.FALSE;
    }
  }
}
