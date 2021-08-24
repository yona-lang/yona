package yona.json;

import com.oracle.truffle.api.nodes.Node;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.tree.ErrorNode;
import yona.runtime.Dict;
import yona.runtime.Seq;
import yona.runtime.Unit;
import yona.runtime.exceptions.JSONParserException;

public class JSONParserVisitor extends JSONBaseVisitor<Object> {
  private final Node node;

  public JSONParserVisitor(Node node) {
    this.node = node;
  }

  @Override
  public Dict visitObj(JSONParser.ObjContext ctx) {
    Dict dict = Dict.EMPTY;
    for (JSONParser.PairContext pairContext : ctx.pair()) {
      Object[] pair = visitPair(pairContext);
      dict = dict.add(pair[0], pair[1]);
    }
    return dict;
  }

  @Override
  public Object[] visitPair(JSONParser.PairContext ctx) {
    Seq key = normalizeString(ctx.STRING().getText());
    Object value = visitValue(ctx.value());
    return new Object[]{key, value};
  }

  @Override
  public Seq visitArr(JSONParser.ArrContext ctx) {
    Seq seq = Seq.EMPTY;
    for (ParserRuleContext innerContext : ctx.value()) {
      seq = seq.insertLast(visit(innerContext));
    }
    return seq;
  }

  @Override
  public Object visitValue(JSONParser.ValueContext ctx) {
    if (ctx.NULL() != null) {
      return Unit.INSTANCE;
    } else if (ctx.NUMBER() != null) {
      try {
        return Long.parseLong(ctx.NUMBER().getText());
      } catch (NumberFormatException e) {
        return Double.parseDouble(ctx.NUMBER().getText());
      }
    } else if (ctx.STRING() != null) {
      return normalizeString(ctx.STRING().getText());
    } else if (ctx.obj() != null) {
      return visitObj(ctx.obj());
    } else if (ctx.bool() != null) {
      return visitBool(ctx.bool());
    } else {
      return visitArr(ctx.arr());
    }
  }

  @Override
  public Boolean visitBool(JSONParser.BoolContext ctx) {
    if (ctx.TRUE() != null) {
      return Boolean.TRUE;
    } else {
      return Boolean.FALSE;
    }
  }

  private Seq normalizeString(String st) {
    return Seq.fromCharSequence(st.substring(1, st.length() - 1));
  }

  @Override
  public Object visitErrorNode(ErrorNode node) {
    throw new JSONParserException(node.getText(), this.node);
  }
}
