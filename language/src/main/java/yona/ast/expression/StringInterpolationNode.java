package yona.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.TypesGen;
import yona.ast.ExpressionNode;
import yona.runtime.DependencyUtils;
import yona.runtime.Seq;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.NoMatchException;
import yona.runtime.strings.StringUtil;

import java.util.Objects;

@NodeInfo(shortName = "stringInterpolation")
public final class StringInterpolationNode extends ExpressionNode {
  @Child
  public ExpressionNode interpolationExpression;
  @Child
  public ExpressionNode alignmentExpression;

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
    Object interpolationValue = TypesGen.ensureNotNull(interpolationExpression.executeGeneric(frame));
    Object alignmentValue = alignmentExpression != null ? alignmentExpression.executeGeneric(frame) : null;

    if (interpolationValue instanceof Promise || alignmentValue instanceof Promise) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      return Promise.all(new Object[]{interpolationValue, alignmentValue}, this).map(fulfilled -> {
        try {
          Object[] fulfiledArgs = (Object[]) fulfilled;
          Seq fulfiledInterpolationValue = StringUtil.yonaValueAsYonaString(fulfiledArgs[0]);
          Object fulfiledAlignmentValue = fulfiledArgs[1];

          if (fulfiledAlignmentValue == null) {
            return fulfiledInterpolationValue;
          } else {
            CompilerDirectives.transferToInterpreterAndInvalidate();
            return Seq.fromCharSequence(String.format("%" + TypesGen.expectLong(fulfiledAlignmentValue) + "s", fulfiledInterpolationValue.asJavaString(this)));
          }
        } catch (UnexpectedResultException e) {
          throw new NoMatchException(e, this, fulfilled);
        }
      }, this);
    } else {
      try {
        Seq interpolationValueString = StringUtil.yonaValueAsYonaString(interpolationValue);
        if (alignmentValue == null) {
          return interpolationValueString;
        } else {
          CompilerDirectives.transferToInterpreterAndInvalidate();
          return Seq.fromCharSequence(String.format("%" + TypesGen.expectLong(alignmentValue) + "s", interpolationValueString.asJavaString(this)));
        }
      } catch (UnexpectedResultException e) {
        throw new NoMatchException(e, this, interpolationValue);
      }
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(interpolationExpression, alignmentExpression);
  }
}
