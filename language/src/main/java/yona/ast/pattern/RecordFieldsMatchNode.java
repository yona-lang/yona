package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.runtime.*;

import java.util.Arrays;
import java.util.Objects;

import static com.oracle.truffle.api.CompilerDirectives.CompilationFinal;

@NodeInfo(shortName = "recordFieldsMatch")
public final class RecordFieldsMatchNode extends MatchNode {
  @CompilationFinal
  private final String recordType;
  @Children
  public RecordPatternFieldNode[] fieldMatchNodes;
  @Children
  private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public RecordFieldsMatchNode(String recordType, RecordPatternFieldNode[] fieldMatchNodes, ExpressionNode[] moduleStack) {
    this.recordType = recordType;
    this.fieldMatchNodes = fieldMatchNodes;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    RecordFieldsMatchNode that = (RecordFieldsMatchNode) o;
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
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(fieldMatchNodes);
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Tuple) {
      Tuple tuple = (Tuple) value;

      String[] recordFields = null;
      if (moduleStack.length > 0) {
        for (int i = moduleStack.length - 1; i >= 0; i--) {
          try {
            YonaModule module = moduleStack[i].executeModule(frame);
            if (module.getRecords().contains(recordType)) {
              recordFields = (String[]) module.getRecords().lookup(recordType);
            }
          } catch (UnexpectedResultException e) {
            continue;
          } catch (YonaException e) {  // IO error
            continue;
          }
        }
      }

      if (recordFields != null) {
        Context context = lookupContextReference(YonaLanguage.class).get();
        Symbol recordTypeSymbol = context.symbol(recordType);

        if (tuple.get(0) instanceof Symbol && recordTypeSymbol.equals((tuple.get(0))) && recordFields.length + 1 == tuple.length()) {
          Seq aliases = Seq.EMPTY;

          boolean matched = false;
          for (RecordPatternFieldNode fieldMatchNode : fieldMatchNodes) {
            MatchResult matchResult = fieldMatchNode.match(new Object[]{tuple, recordFields}, frame);
            if (!matchResult.isMatches()) {
              continue;
            } else {
              matched = true;
              aliases = Seq.catenate(aliases, Seq.sequence((Object[]) matchResult.getAliases()));
            }
          }

          if (matched) {
            aliases.foldLeft(null, (acc, alias) -> {
              ((AliasNode) alias).executeGeneric(frame);
              return null;
            });

            return MatchResult.TRUE;
          }
        }
      }
    }

    return MatchResult.FALSE;
  }

  @Override
  protected String[] providedIdentifiers() {
    return DependencyUtils.catenateProvidedIdentifiers(fieldMatchNodes);
  }

  public static final class RecordPatternFieldNode extends MatchNode {
    @CompilationFinal
    String fieldName;
    @Child
    MatchNode fieldValue;

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
    protected String[] requiredIdentifiers() {
      return fieldValue.getRequiredIdentifiers();
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

    @Override
    protected String[] providedIdentifiers() {
      return fieldValue.getProvidedIdentifiers();
    }
  }
}
