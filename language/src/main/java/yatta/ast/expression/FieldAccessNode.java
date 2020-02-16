package yatta.ast.expression;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.runtime.Symbol;
import yatta.runtime.Tuple;
import yatta.runtime.YattaModule;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.InvalidRecordException;
import yatta.runtime.exceptions.NoRecordException;
import yatta.runtime.exceptions.NoRecordFieldException;

import java.util.Objects;

import static com.oracle.truffle.api.CompilerDirectives.CompilationFinal;

@NodeInfo(shortName = "fieldAccess")
public class FieldAccessNode extends ExpressionNode {
  @Child private ExpressionNode recordName;
  @CompilationFinal private final String fieldName;
  @Children private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public FieldAccessNode(IdentifierNode recordName, String fieldName, ExpressionNode[] moduleStack) {
    this.recordName = recordName;
    this.fieldName = fieldName;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FieldAccessNode that = (FieldAccessNode) o;
    return Objects.equals(recordName, that.recordName) &&
        Objects.equals(fieldName, that.fieldName);
  }

  @Override
  public int hashCode() {
    return Objects.hash(recordName, fieldName);
  }

  @Override
  public String toString() {
    return "FieldAccessNode{" +
        "recordName='" + recordName + '\'' +
        ", fieldName='" + fieldName + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    CompilerAsserts.compilationConstant(fieldName);
    Object recordValue = recordName.executeGeneric(frame);

    if (recordValue instanceof Tuple) {
      Tuple recordTuple = (Tuple) recordValue;
      if (recordTuple.length() <= 1) {
        throw new InvalidRecordException(recordValue, this);
      } else {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        MaterializedFrame materializedFrame = frame.materialize();

        return getFieldElementFromTuple(recordTuple, materializedFrame);
      }
    } else if (recordValue instanceof Promise) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      MaterializedFrame materializedFrame = frame.materialize();

      return  ((Promise) recordValue).map(recordTupleObj -> {
        if (recordTupleObj instanceof Tuple) {
          Tuple recordTuple = (Tuple) recordTupleObj;
          return getFieldElementFromTuple(recordTuple, materializedFrame);
        } else {
          return YattaException.typeError(this, recordTupleObj);
        }
      }, this);
    } else {
      throw YattaException.typeError(this, recordValue);
    }
  }

  private Object getFieldElementFromTuple(Tuple recordTuple, MaterializedFrame frame) {
    if (recordTuple.get(0) instanceof Symbol) {
      Symbol recordTypeSymbol = (Symbol) recordTuple.get(0);
      return getFieldValue(getRecordFields(recordTypeSymbol.asString(), frame), recordTuple);
    } else if (recordTuple.get(0) instanceof Promise) {
      return Promise.all(recordTuple.toArray(), this).map(recordElementsObj -> {
        Object[] recordElements = (Object[]) recordElementsObj;
        if (recordElements[0] instanceof Symbol) {
          return getFieldValue(getRecordFields(((Symbol) recordElements[0]).asString(), frame), recordTuple);
        } else {
          return new InvalidRecordException(recordTuple, this);
        }
      }, this);
    } else {
      throw new InvalidRecordException(recordTuple, this);
    }
  }

  private String[] getRecordFields(String recordType, VirtualFrame frame) {
    if (moduleStack.length > 0) {
      for (int i = moduleStack.length - 1; i >= 0; i--) {
        try {
          YattaModule module = moduleStack[i].executeModule(frame);
          if (module.getRecords().contains(recordType)) {
            return (String[]) module.getRecords().lookup(recordType);
          }
        } catch (UnexpectedResultException e) {
          continue;
        } catch (YattaException e) {  // IO error
          continue;
        }
      }
    }

    throw new NoRecordException(recordType, this);
  }

  private Object getFieldValue(String[] recordFields, Tuple recordTuple) {
    for (int i = 0; i < recordFields.length; i++) {
      if (fieldName.equals(recordFields[i])) {
        return recordTuple.get(i + 1);
      }
    }

    throw new NoRecordFieldException(recordName.toString(), fieldName, this);
  }
}
