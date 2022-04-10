package yona.ast.expression.value;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.ast.ExpressionNode;
import yona.runtime.Tuple;

import java.util.Objects;

@NodeInfo(shortName = "recordFieldValue")
public final class RecordFieldValueNode extends ExpressionNode {
  @CompilerDirectives.CompilationFinal
  String fieldName;
  @Child ExpressionNode fieldValue;

  public RecordFieldValueNode(String fieldName, ExpressionNode fieldValue) {
    this.fieldName = fieldName;
    this.fieldValue = fieldValue;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    RecordFieldValueNode that = (RecordFieldValueNode) o;
    return Objects.equals(fieldName, that.fieldName) &&
        Objects.equals(fieldValue, that.fieldValue);
  }

  @Override
  public int hashCode() {
    return Objects.hash(fieldName, fieldValue);
  }

  @Override
  public String toString() {
    return "RecordInstanceFieldNode{" +
        "fieldName=" + fieldName +
        ", fieldValue=" + fieldValue +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return Tuple.allocate(this, fieldName, fieldValue.executeGeneric(frame));
  }

  @Override
  protected String[] requiredIdentifiers() {
    return fieldValue.getRequiredIdentifiers();
  }

  @Override
  public Tuple executeTuple(VirtualFrame frame) throws UnexpectedResultException {
    return Tuple.allocate(this, fieldName, fieldValue.executeGeneric(frame));
  }
}
