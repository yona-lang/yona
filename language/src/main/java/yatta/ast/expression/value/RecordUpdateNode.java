package yatta.ast.expression.value;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.TypesGen;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.runtime.DependencyUtils;
import yatta.runtime.Symbol;
import yatta.runtime.Tuple;
import yatta.runtime.YattaModule;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.InvalidRecordException;
import yatta.runtime.exceptions.NoRecordException;
import yatta.runtime.exceptions.NoRecordFieldException;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo(shortName = "recordInstance")
public final class RecordUpdateNode extends ExpressionNode {
  @Child
  private ExpressionNode recordIdentifier;
  @Children
  private final RecordFieldValueNode[] fields;
  @Children
  private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public RecordUpdateNode(ExpressionNode recordIdentifier, RecordFieldValueNode[] fields, ExpressionNode[] moduleStack) {
    this.recordIdentifier = recordIdentifier;
    this.fields = fields;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    RecordUpdateNode that = (RecordUpdateNode) o;
    return Objects.equals(recordIdentifier, that.recordIdentifier) &&
        Arrays.equals(fields, that.fields);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(recordIdentifier);
    result = 31 * result + Arrays.hashCode(fields);
    return result;
  }

  @Override
  public String toString() {
    return "RecordUpdateNode{" +
        "recordIdentifier=" + recordIdentifier +
        ", fields=" + Arrays.toString(fields) +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object originalRecordValue = recordIdentifier.executeGeneric(frame);
    if (originalRecordValue instanceof Tuple) {
      try {
        return executeTupleValue(frame, (Tuple) originalRecordValue);
      } catch (UnexpectedResultException e) {
        throw YattaException.typeError(this, e.getResult());
      }
    } else if (originalRecordValue instanceof Promise) {
      Promise originalRecordPromise = (Promise) originalRecordValue;
      CompilerDirectives.transferToInterpreterAndInvalidate();
      MaterializedFrame materializedFrame = frame.materialize();

      return originalRecordPromise.map(val -> {
        if (val instanceof Tuple) {
          try {
            return executeTupleValue(materializedFrame, (Tuple) val);
          } catch (UnexpectedResultException e) {
            return YattaException.typeError(this, e.getResult());
          }
        } else {
          return YattaException.typeError(this, val);
        }
      }, this);
    } else {
      throw YattaException.typeError(this, originalRecordValue);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiersWith(recordIdentifier, fields);
  }

  private Tuple executeTupleValue(VirtualFrame frame, Tuple originalRecordTuple) throws UnexpectedResultException {
    Symbol recordTypeSymbol = TypesGen.expectSymbol(originalRecordTuple.get(0));
    String recordType = recordTypeSymbol.asString();

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

    if (recordFields == null) {
      throw new NoRecordException(recordType, this);
    } else {
      if (originalRecordTuple.length() != recordFields.length + 1) {
        throw new InvalidRecordException(originalRecordTuple, this);
      }

      Object[] resultFields = originalRecordTuple.toArray().clone();
      for (RecordFieldValueNode field : fields) {
        Tuple fieldTuple = field.executeTuple(frame);
        setField(recordType, (String) fieldTuple.get(0), fieldTuple.get(1), recordFields, resultFields);
      }

      return new Tuple(resultFields);
    }
  }

  private void setField(String recordType, String fieldName, Object fieldValue, String[] recordFields, Object[] resultFields) {
    int fieldPos = -1;
    for (int i = 0; i < recordFields.length; i++) {
      if (fieldName.equals(recordFields[i])) {
        fieldPos = i;
      }
    }

    if (fieldPos == -1) {
      throw new NoRecordFieldException(recordType, fieldName, this);
    } else {
      resultFields[fieldPos + 1] = fieldValue;
    }
  }
}
