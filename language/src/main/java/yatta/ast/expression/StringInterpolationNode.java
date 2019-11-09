package yatta.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.TypesGen;
import yatta.ast.ExpressionNode;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.NoMatchException;
import yatta.runtime.strings.StringUtil;

import java.util.Objects;

public final class StringInterpolationNode extends ExpressionNode {
  @Child
  ExpressionNode interpolationExpression;
  @Child
  ExpressionNode alignmentExpression;

  public StringInterpolationNode(ExpressionNode interpolationExpression, ExpressionNode alignmentExpression) {
    this.interpolationExpression = interpolationExpression;
    this.alignmentExpression = alignmentExpression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    StringInterpolationNode that = (StringInterpolationNode) o;
    return Objects.equals(interpolationExpression, that.interpolationExpression) &&
        Objects.equals(alignmentExpression, that.alignmentExpression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(interpolationExpression, alignmentExpression);
  }

  @Override
  public String toString() {
    return "StringInterpolationNode{" +
        "interpolationExpression=" + interpolationExpression +
        ", alignmentExpression=" + alignmentExpression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object interpolationValue = interpolationExpression.executeGeneric(frame);
    Object alignmentValue = alignmentExpression != null ? alignmentExpression.executeGeneric(frame) : null;

    if (interpolationValue instanceof Promise || alignmentValue instanceof Promise) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      return Promise.all(new Object[]{interpolationValue, alignmentValue}, this).map(fulfiled -> {
        try {
          Object[] fulfiledArgs = (Object[]) fulfiled;
          Seq fulfiledInterpolationValue = StringUtil.yattaValueAsYattaString(fulfiledArgs[0]);
          Object fulfiledAlignmentValue = fulfiledArgs[1];

          if (fulfiledAlignmentValue == null) {
            return fulfiledInterpolationValue;
          } else {
            return Seq.fromCharSequence(String.format("%" + TypesGen.expectLong(fulfiledAlignmentValue) + "s", fulfiledInterpolationValue.asJavaString(this)));
          }
        } catch (UnexpectedResultException e) {
          throw new NoMatchException(e, this);
        }
      }, this);
    } else {
      try {
        Seq interpolationValueString = StringUtil.yattaValueAsYattaString(interpolationValue);
        if (alignmentValue == null) {
          return interpolationValueString;
        } else {
          return Seq.fromCharSequence(String.format("%" + TypesGen.expectLong(alignmentValue) + "s", interpolationValueString.asJavaString(this)));
        }
      } catch (UnexpectedResultException e) {
        throw new NoMatchException(e, this);
      }
    }
  }
}
