package abzu.ast;

import abzu.AbzuBaseVisitor;
import abzu.AbzuParser;
import abzu.ast.expression.*;
import abzu.ast.expression.value.*;

import java.util.ArrayList;
import java.util.List;

public final class ParserVisitor extends AbzuBaseVisitor<AbzuExpressionNode> {

  @Override
  public AbzuExpressionNode visitInput(AbzuParser.InputContext ctx) {
    return ctx.expression().accept(this);
  }

  @Override
  public NegationNode visitUnaryOperationExpression(AbzuParser.UnaryOperationExpressionContext ctx) {
    return new NegationNode(ctx.expression().accept(this));
  }

  @Override
  public FunctionApplicationNode visitFunctionApplicationExpression(AbzuParser.FunctionApplicationExpressionContext ctx) {
    List<AbzuExpressionNode> args = new ArrayList<>();
    for (AbzuParser.ExpressionContext exprCtx : ctx.apply().expression()) {
      args.add(exprCtx.accept(this));
    }
    return new FunctionApplicationNode(ctx.apply().NAME().getText(), args.toArray(new AbzuExpressionNode[]{}));
  }

  @Override
  public ConditionNode visitConditionalExpression(AbzuParser.ConditionalExpressionContext ctx) {
    AbzuExpressionNode ifNode = ctx.conditional().ifX.accept(this);
    AbzuExpressionNode thenNode = ctx.conditional().thenX.accept(this);
    AbzuExpressionNode elseNode = ctx.conditional().elseX.accept(this);
    return new ConditionNode(ifNode, thenNode, elseNode);
  }

  @Override
  public BinaryOperationNode visitBinaryOperationExpression(AbzuParser.BinaryOperationExpressionContext ctx) {
    AbzuExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    AbzuExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    return new BinaryOperationNode(left, right, ctx.BIN_OP().getText());
  }

  @Override
  public LetNode visitLetExpression(AbzuParser.LetExpressionContext ctx) {
    List<AliasNode> aliases = new ArrayList<>();
    for (AbzuParser.AliasContext aliasCtx : ctx.let().alias()) {
      aliases.add(new AliasNode(aliasCtx.NAME().getText(), aliasCtx.expression().accept(this)));
    }
    return new LetNode(aliases.toArray(new AliasNode[]{}), ctx.let().expression().accept(this));
  }

  @Override
  public UnitNode visitUnit(AbzuParser.UnitContext ctx) {
    return UnitNode.INSTANCE;
  }

  @Override
  public Int64Node visitInt64(AbzuParser.Int64Context ctx) {
    return new Int64Node(Long.parseLong(ctx.INTEGER().getText()));
  }

  @Override
  public Float64Node visitFloat64(AbzuParser.Float64Context ctx) {
    return new Float64Node(Double.parseDouble(ctx.FLOAT().getText()));
  }

  @Override
  public StringNode visitString(AbzuParser.StringContext ctx) {
    return new StringNode(ctx.STRING().getText());
  }

  @Override
  public BoolNode visitBool(AbzuParser.BoolContext ctx) {
    return ctx.KW_TRUE() != null ? BoolNode.TRUE : BoolNode.FALSE;
  }

  @Override
  public FunctionNode visitFunction(AbzuParser.FunctionContext ctx) {
    List<String> args = new ArrayList<>();
    for (AbzuParser.ArgContext argCtx : ctx.arg()) {
      args.add(argCtx.NAME().getText());
    }
    return new FunctionNode(ctx.NAME().getText(), args, ctx.expression().accept(this));
  }

  @Override
  public TupleNode visitTuple(AbzuParser.TupleContext ctx) {
    int elementsCount = ctx.expression().size();
    AbzuExpressionNode[] content = new AbzuExpressionNode[elementsCount];
    for (int i = 0; i < elementsCount; i++) {
      content[i] = ctx.expression(i).accept(this);
    }
    return new TupleNode(content);
  }

  @Override
  public DictNode visitDict(AbzuParser.DictContext ctx) {
    List<DictNode.Entry> entries = new ArrayList<>();
    for (int i = 0; i < ctx.key().size(); i++) {
      AbzuParser.KeyContext keyCtx = ctx.key(i);
      AbzuParser.ExpressionContext expressionCtx = ctx.expression(i);
      entries.add(new DictNode.Entry(keyCtx.STRING().getText(), expressionCtx.accept(this)));
    }
    return new DictNode(entries);
  }

  @Override
  public ListNode visitList(AbzuParser.ListContext ctx) {
    List<AbzuExpressionNode> expressions = new ArrayList<>();
    for (AbzuParser.ExpressionContext expr : ctx.expression()) {
      expressions.add(expr.accept(this));
    }
    return new ListNode(expressions.toArray(new AbzuExpressionNode[]{}));
  }
}
