package abzu.runtime;

import abzu.ast.ExpressionNode;
import abzu.ast.expression.value.*;
import sun.reflect.generics.reflectiveObjects.NotImplementedException;

public class NodeMaker {
  public static ExpressionNode makeNode(Object val) {
    if (val instanceof Integer) {
      return new IntegerNode((Integer) val);
    } else if (val instanceof Long) {
      return new IntegerNode((Long) val);
    } else if (val instanceof Float) {
      return new FloatNode((Float) val);
    } else if (val instanceof Double) {
      return new FloatNode((Double) val);
    } else if (val instanceof Boolean) {
      return ((Boolean) val).booleanValue() ? BooleanNode.TRUE : BooleanNode.FALSE;
    } else if (val instanceof Byte) {
      return new ByteNode((Byte) val);
    } else if (val instanceof String) {
      return new StringNode((String) val);
    } else if (val instanceof Tuple) {
      Tuple tuple = (Tuple) val;
      ExpressionNode[] expressions = new ExpressionNode[tuple.size()];
      for (int i = 0; i < expressions.length; i++) {
        expressions[i] = makeNode(tuple.get(i));
      }

      return new TupleNode(expressions);
    } else if (val instanceof Sequence) {
      Sequence sequence = (Sequence) val;

      if (sequence.length() == 0) {
        return EmptySequenceNode.INSTANCE;
      } else if (sequence.length() == 1) {
        return new OneSequenceNode(makeNode(sequence.first()));
      } else if (sequence.length() == 2) {
        return new TwoSequenceNode(makeNode(sequence.first()), makeNode(sequence.removeFirst().first()));
      } else {

        ExpressionNode[] expressions = new ExpressionNode[sequence.length()];
        for (int i = 0; i < sequence.length(); i++) {
          expressions[i] = makeNode(sequence.lookup(i));
        }

        return new SequenceNode(expressions);
      }
    } else {
      throw new NotImplementedException();
    }
  }
}
