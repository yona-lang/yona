package abzu.ast;

import abzu.AbzuBaseVisitor;
import abzu.AbzuLanguage;
import abzu.AbzuParser;
import abzu.ast.builtin.BuiltinNode;
import abzu.ast.call.InvokeNode;
import abzu.ast.call.ModuleCallNode;
import abzu.ast.controlflow.BlockNode;
import abzu.ast.expression.*;
import abzu.ast.expression.value.*;
import abzu.ast.local.ReadArgumentNode;
import abzu.ast.local.WriteLocalVariableNodeGen;
import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.FrameSlotKind;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import org.antlr.v4.runtime.tree.TerminalNode;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class ParserVisitor extends AbzuBaseVisitor<ExpressionNode> {
  private AbzuLanguage language;
  private Source source;
  private int lambdaCount = 0;

  public ParserVisitor(AbzuLanguage language, Source source) {
    this.language = language;
    this.source = source;
  }

  @Override
  public ExpressionNode visitInput(AbzuParser.InputContext ctx) {
    ExpressionNode functionBodyNode = ctx.expression().accept(this);
    functionBodyNode.addRootTag();

    FunctionNode mainFunctionNode = new FunctionNode(language, source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), "$main", Collections.emptyList(), new FrameDescriptor(), functionBodyNode);
    return new InvokeNode(language, mainFunctionNode, new ExpressionNode[] {});
  }

  @Override
  public NegationNode visitUnaryOperationExpression(AbzuParser.UnaryOperationExpressionContext ctx) {
    return new NegationNode(ctx.expression().accept(this));
  }

  @Override
  public ExpressionNode visitFunctionApplicationExpression(AbzuParser.FunctionApplicationExpressionContext ctx) {
    List<ExpressionNode> args = new ArrayList<>();
    for (AbzuParser.ExpressionContext exprCtx : ctx.apply().expression()) {
      args.add(exprCtx.accept(this));
    }

    ExpressionNode[] argNodes = args.toArray(new ExpressionNode[]{});

    if(ctx.apply().moduleCall() != null) {
      FQNNode fqnNode = visitFqn(ctx.apply().moduleCall().fqn());
      String functionName = ctx.apply().moduleCall().NAME().getText();
      return new ModuleCallNode(language, fqnNode, functionName, argNodes);
    } else {
      String functionName = ctx.apply().NAME().getText();
      NodeFactory<? extends BuiltinNode> builtinFunction = language.getContextReference().get().getBuiltins().lookup(functionName);

      if (builtinFunction != null) {
        return builtinFunction.createNode((Object) argNodes);
      } else {
        return new InvokeNode(language, new IdentifierNode(language, functionName), argNodes);
      }
    }
  }

  @Override
  public ConditionNode visitConditionalExpression(AbzuParser.ConditionalExpressionContext ctx) {
    ExpressionNode ifNode = ctx.conditional().ifX.accept(this);
    ExpressionNode thenNode = ctx.conditional().thenX.accept(this);
    ExpressionNode elseNode = ctx.conditional().elseX.accept(this);
    return new ConditionNode(ifNode, thenNode, elseNode);
  }

  @Override
  public BinaryOperationNode visitBinaryOperationExpression(AbzuParser.BinaryOperationExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    return new BinaryOperationNode(left, right, ctx.BIN_OP().getText());
  }

  @Override
  public LetNode visitLetExpression(AbzuParser.LetExpressionContext ctx) {
    AliasNode[] aliasNodes = new AliasNode[ctx.let().alias().size()];

    int i = 0;
    for (AbzuParser.AliasContext aliasCtx : ctx.let().alias()) {
      String alias = aliasCtx.NAME().getText();
      aliasNodes[i] = new AliasNode(alias, aliasCtx.expression().accept(this));
      i++;
    }

    return new LetNode(aliasNodes, ctx.let().expression().accept(this));
  }

  @Override
  public UnitNode visitUnit(AbzuParser.UnitContext ctx) {
    return UnitNode.INSTANCE;
  }

  @Override
  public IntegerNode visitIntegerLiteral(AbzuParser.IntegerLiteralContext ctx) {
    return new IntegerNode(Long.parseLong(ctx.INTEGER().getText()));
  }

  @Override
  public FloatNode visitFloatLiteral(AbzuParser.FloatLiteralContext ctx) {
    return new FloatNode(Double.parseDouble(ctx.FLOAT().getText()));
  }

  @Override
  public ExpressionNode visitByteLiteral(AbzuParser.ByteLiteralContext ctx) {
    return new ByteNode(Byte.parseByte(ctx.INTEGER().getText()));
  }

  @Override
  public StringNode visitStringLiteral(AbzuParser.StringLiteralContext ctx) {
    return new StringNode(normalizeString(ctx.STRING().getText()));
  }

  @Override
  public BooleanNode visitBooleanLiteral(AbzuParser.BooleanLiteralContext ctx) {
    return ctx.KW_TRUE() != null ? BooleanNode.TRUE : BooleanNode.FALSE;
  }

  @Override
  public FunctionNode visitFunction(AbzuParser.FunctionContext ctx) {
    return newFunction(ctx.arg(), source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), ctx.NAME().getText(), ctx.expression().accept(this));
  }

  @Override
  public FunctionNode visitLambda(AbzuParser.LambdaContext ctx) {
    return newFunction(ctx.arg(), source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), "$lambda-" + lambdaCount++, ctx.expression().accept(this));
  }

  private FunctionNode newFunction(List<AbzuParser.ArgContext> argContexts, SourceSection sourceSection, String functionName, ExpressionNode functionBodyNode) {
    FrameDescriptor frameDescriptor = new FrameDescriptor();
    List<String> args = new ArrayList<>(argContexts.size());
    for (AbzuParser.ArgContext argCtx : argContexts) {
      args.add(argCtx.NAME().getText());
    }

    ExpressionNode[] bodyStatements = new ExpressionNode[args.size() + 1];

    for (int i = 0; i < args.size(); i++) {
      ReadArgumentNode readArg = new ReadArgumentNode(i);
      FrameSlot frameSlot = frameDescriptor.findOrAddFrameSlot(args.get(i), i, FrameSlotKind.Illegal);
      bodyStatements[i] = WriteLocalVariableNodeGen.create(readArg, frameSlot);
    }

    functionBodyNode.addRootTag();
    bodyStatements[args.size()] = functionBodyNode;

    return new FunctionNode(language, sourceSection, functionName, args, frameDescriptor, new BlockNode(bodyStatements));
  }

  @Override
  public TupleNode visitTuple(AbzuParser.TupleContext ctx) {
    int elementsCount = ctx.expression().size();
    ExpressionNode[] content = new ExpressionNode[elementsCount];
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
    List<ExpressionNode> expressions = new ArrayList<>();
    for (AbzuParser.ExpressionContext expr : ctx.expression()) {
      expressions.add(expr.accept(this));
    }
    return new ListNode(expressions.toArray(new ExpressionNode[]{}));
  }

  @Override
  public ModuleNode visitModule(AbzuParser.ModuleContext ctx) {
    FQNNode moduleFQN = visitFqn(ctx.fqn());
    NonEmptyStringListNode exports = visitNonEmptyListOfNames(ctx.nonEmptyListOfNames());

    int elementsCount = ctx.function().size();
    FunctionNode[] functions = new FunctionNode[elementsCount];
    for (int i = 0; i < elementsCount; i++) {
      functions[i] = visitFunction(ctx.function(i));
    }
    return new ModuleNode(moduleFQN, exports, functions);
  }

  @Override
  public NonEmptyStringListNode visitNonEmptyListOfNames(AbzuParser.NonEmptyListOfNamesContext ctx) {
    List<String> names = new ArrayList<>();
    for (TerminalNode text : ctx.NAME()) {
      names.add(text.getText());
    }
    return new NonEmptyStringListNode(names.toArray(new String[]{}));
  }

  @Override
  public FQNNode visitFqn(AbzuParser.FqnContext ctx) {
    int elementsCount = ctx.NAME().size();
    String[] content = new String[elementsCount];
    for (int i = 0; i < elementsCount; i++) {
      content[i] = ctx.NAME(i).getText();
    }
    return new FQNNode(language, content);
  }

  @Override
  public SymbolNode visitSymbol(AbzuParser.SymbolContext ctx) {
    return new SymbolNode(ctx.NAME().getText());
  }

  @Override
  public IdentifierNode visitIdentifier(AbzuParser.IdentifierContext ctx) {
    String name = ctx.NAME().getText();
    return new IdentifierNode(language, name);
  }

  private String normalizeString(String str) {
    return str.substring(1, str.length() - 1);
  }
}
