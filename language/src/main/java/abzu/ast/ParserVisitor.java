package abzu.ast;

import abzu.AbzuBaseVisitor;
import abzu.AbzuLanguage;
import abzu.AbzuParser;
import abzu.ast.call.InvokeNode;
import abzu.ast.controlflow.BlockNode;
import abzu.ast.expression.*;
import abzu.ast.expression.value.*;
import abzu.ast.local.ReadArgumentNode;
import abzu.ast.local.ReadLocalVariableNodeGen;
import abzu.ast.local.WriteLocalVariableNodeGen;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.FrameSlotKind;
import com.oracle.truffle.api.source.Source;
import org.antlr.v4.runtime.tree.TerminalNode;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class ParserVisitor extends AbzuBaseVisitor<ExpressionNode> {
  private AbzuLanguage language;
  private Source source;
  private LexicalScope lexicalScope;

  /**
   * Local variable names that are visible in the current block. Variables are not visible outside
   * of their defining block, to prevent the usage of undefined variables.
   */
  static class LexicalScope {
    private final LexicalScope outer;
    private final Map<String, FrameSlot> locals;

    LexicalScope(LexicalScope outer) {
      this.outer = outer;
      this.locals = new HashMap<>();
      if (outer != null) {
        locals.putAll(outer.locals);
      }
    }

    FrameSlot get(String key) {
      if (locals.containsKey(key)) {
        return locals.get(key);
      } else {
        return outer.get(key);
      }
    }

    void put(String key, FrameSlot frameSlot) {
      locals.put(key, frameSlot);
    }
  }

  public ParserVisitor(AbzuLanguage language, Source source) {
    this.language = language;
    this.source = source;
    this.lexicalScope = new LexicalScope(null);
  }

  @Override
  public ExpressionNode visitInput(AbzuParser.InputContext ctx) {
    return ctx.expression().accept(this);
  }

  @Override
  public NegationNode visitUnaryOperationExpression(AbzuParser.UnaryOperationExpressionContext ctx) {
    return new NegationNode(ctx.expression().accept(this));
  }

  @Override
  public InvokeNode visitFunctionApplicationExpression(AbzuParser.FunctionApplicationExpressionContext ctx) {
    List<ExpressionNode> args = new ArrayList<>();
    for (AbzuParser.ExpressionContext exprCtx : ctx.apply().expression()) {
      args.add(exprCtx.accept(this));
    }
    return new InvokeNode(new StringNode(ctx.apply().NAME().getText()), args.toArray(new ExpressionNode[]{}));
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
    this.lexicalScope = new LexicalScope(this.lexicalScope);
    List<String> args = new ArrayList<>(ctx.arg().size());
    for (AbzuParser.ArgContext argCtx : ctx.arg()) {
      args.add(argCtx.NAME().getText());
    }

    FrameDescriptor frameDescriptor = new FrameDescriptor();
    ExpressionNode[] bodyStatements = new ExpressionNode[args.size() + 1];

    for (int i = 0; i < args.size(); i++) {
      ReadArgumentNode readArg = new ReadArgumentNode(i);
      FrameSlot frameSlot = frameDescriptor.findOrAddFrameSlot(args.get(i), i, FrameSlotKind.Illegal);
      lexicalScope.put(args.get(i), frameSlot);
      bodyStatements[i] = WriteLocalVariableNodeGen.create(readArg, frameSlot);
    }

    ExpressionNode functionBodyNode = ctx.expression().accept(this);
    functionBodyNode.addRootTag();
    bodyStatements[args.size()] = functionBodyNode;

    this.lexicalScope = this.lexicalScope.outer;
    return new FunctionNode(language, source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), ctx.NAME().getText(), args, frameDescriptor, new BlockNode(bodyStatements));
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
    FQNNode fqn = visitFqn(ctx.fqn());
    NonEmptyStringListNode exports = visitNonEmptyListOfNames(ctx.nonEmptyListOfNames());

    int elementsCount = ctx.function().size();
    FunctionNode[] functions = new FunctionNode[elementsCount];
    for (int i = 0; i < elementsCount; i++) {
      functions[i] = visitFunction(ctx.function(i));
    }
    return new ModuleNode(fqn, exports, functions);
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
    return new FQNNode(content);
  }

  @Override
  public SymbolNode visitSymbol(AbzuParser.SymbolContext ctx) {
    return new SymbolNode(ctx.NAME().getText());
  }

  @Override
  public ExpressionNode visitIdentifier(AbzuParser.IdentifierContext ctx) {
    return ReadLocalVariableNodeGen.create(this.lexicalScope.get(ctx.NAME().getText()));
  }

  private String normalizeString(String str) {
    return str.substring(1, str.length() - 1);
  }
}
