package abzu.parser;

import abzu.AbzuBaseVisitor;
import abzu.AbzuLanguage;
import abzu.AbzuParser;
import abzu.ast.ExpressionNode;
import abzu.ast.builtin.BuiltinNode;
import abzu.ast.call.InvokeNode;
import abzu.ast.call.ModuleCallNode;
import abzu.ast.expression.*;
import abzu.ast.expression.value.*;
import abzu.ast.local.ReadArgumentNode;
import abzu.ast.pattern.*;
import abzu.runtime.UninitializedFrameSlot;
import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import org.antlr.v4.runtime.tree.TerminalNode;

import java.util.*;

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

    FunctionNode mainFunctionNode = new FunctionNode(language, source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), "$main", 0, new FrameDescriptor(), functionBodyNode);
    return new InvokeNode(language, mainFunctionNode, new ExpressionNode[]{});
  }

  @Override
  public ExpressionNode visitExpressionInParents(AbzuParser.ExpressionInParentsContext ctx) {
    return ctx.expression().accept(this);
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

    if (ctx.apply().moduleCall() != null) {
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
  public PatternLetNode visitLetExpression(AbzuParser.LetExpressionContext ctx) {
    ExpressionNode[] aliasNodes = new ExpressionNode[ctx.let().alias().size()];

    for (int i = 0; i < ctx.let().alias().size(); i++) {
      aliasNodes[i] = visitAlias(ctx.let().alias(i));
    }

    return new PatternLetNode(aliasNodes, ctx.let().expression().accept(this));
  }

  @Override
  public ExpressionNode visitAlias(AbzuParser.AliasContext ctx) {
    if (ctx.patternAlias() != null) {
      return new PatternAliasNode(visitPattern(ctx.patternAlias().pattern()), ctx.patternAlias().expression().accept(this));
    } else if (ctx.moduleAlias() != null) {
      return new AliasNode(ctx.moduleAlias().NAME().getText(), visitModule(ctx.moduleAlias().module()));
    } else if (ctx.fqnAlias() != null) {
      return new AliasNode(ctx.fqnAlias().NAME().getText(), visitFqn(ctx.fqnAlias().fqn()));
    } else {
      return new AliasNode(ctx.lambdaAlias().NAME().getText(), visitLambda(ctx.lambdaAlias().lambda()));
    }
  }

  @Override
  public UnitNode visitUnit(AbzuParser.UnitContext ctx) {
    return UnitNode.INSTANCE;
  }

  @Override
  public UnderscoreMatchNode visitUnderscore(AbzuParser.UnderscoreContext ctx) {
    return UnderscoreMatchNode.INSTANCE;
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
  public ByteNode visitByteLiteral(AbzuParser.ByteLiteralContext ctx) {
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
  public FunctionNode visitLambda(AbzuParser.LambdaContext ctx) {
    MatchNode[] patterns = new MatchNode[ctx.pattern().size()];
    for (int i = 0; i < ctx.pattern().size(); i++) {
      patterns[i] = visitPattern(ctx.pattern(i));
    }
    TupleMatchNode argPatterns = new TupleMatchNode(patterns);

    ExpressionNode[] argumentNodes = new ExpressionNode[ctx.pattern().size()];
    for (int j = 0; j < argumentNodes.length; j++) {
      argumentNodes[j] = new ReadArgumentNode(j);
    }

    TupleNode argsTuple = new TupleNode(argumentNodes);
    CaseNode caseNode = new CaseNode(argsTuple, new PatternNode[]{new PatternNode(argPatterns, ctx.expression().accept(this))});

    caseNode.addRootTag();

    return new FunctionNode(language, source.createSection(
        ctx.LAMBDA_START().getSymbol().getLine(),
        ctx.LAMBDA_START().getSymbol().getCharPositionInLine() + 1,
        ctx.expression().stop.getLine(),
        ctx.expression().stop.getCharPositionInLine() + 1
    ), "$lambda-" + lambdaCount++, ctx.pattern().size(), new FrameDescriptor(UninitializedFrameSlot.INSTANCE), caseNode);
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
  public EmptySequenceNode visitEmptySequence(AbzuParser.EmptySequenceContext ctx) {
    return EmptySequenceNode.INSTANCE;
  }

  @Override
  public OneSequenceNode visitOneSequence(AbzuParser.OneSequenceContext ctx) {
    return new OneSequenceNode(ctx.expression().accept(this));
  }

  @Override
  public TwoSequenceNode visitTwoSequence(AbzuParser.TwoSequenceContext ctx) {
    return new TwoSequenceNode(ctx.expression(0).accept(this), ctx.expression(1).accept(this));
  }

  @Override
  public SequenceNode visitOtherSequence(AbzuParser.OtherSequenceContext ctx) {
    List<ExpressionNode> expressions = new ArrayList<>();
    for (AbzuParser.ExpressionContext expr : ctx.expression()) {
      expressions.add(expr.accept(this));
    }
    return new SequenceNode(expressions.toArray(new ExpressionNode[]{}));
  }

  @Override
  public ModuleNode visitModule(AbzuParser.ModuleContext ctx) {
    FQNNode moduleFQN = visitFqn(ctx.fqn());
    NonEmptyStringListNode exports = visitNonEmptyListOfNames(ctx.nonEmptyListOfNames());

    int functionPatternsCount = ctx.function().size();
    Map<String, List<PatternNode>> functionPatterns = new HashMap<>();
    Map<String, Integer> functionCardinality = new HashMap<>();
    Map<String, SourceSection> functionSourceSections = new HashMap<>();

    String lastFunctionName = null;

    for (int i = 0; i < functionPatternsCount; i++) {
      AbzuParser.FunctionContext functionContext = ctx.function(i);

      String functionName = functionContext.NAME().getText();

      if (lastFunctionName != null && !lastFunctionName.equals(functionName) && functionPatterns.containsKey(functionName)) {
        throw new AbzuParseError(source,
            functionContext.NAME().getSymbol().getLine(),
            functionContext.NAME().getSymbol().getCharPositionInLine() + 1,
            functionContext.NAME().getText().length(), "Function " + functionName + " was already defined previously.");
      }
      lastFunctionName = functionName;

      if (!functionPatterns.containsKey(functionName)) {
        functionPatterns.put(functionName, new ArrayList<>());
      }

      if (!functionSourceSections.containsKey(functionName)) {
        functionSourceSections.put(functionName, source.createSection(
            functionContext.NAME().getSymbol().getLine(),
            functionContext.NAME().getSymbol().getCharPositionInLine() + 1,
            functionContext.expression().stop.getLine(),
            functionContext.expression().stop.getCharPositionInLine() + 1)
        );
      } else {
        SourceSection sourceSection = functionSourceSections.get(functionName);
        functionSourceSections.put(functionName, source.createSection(
            sourceSection.getStartLine(),
            sourceSection.getStartColumn(),
            functionContext.expression().stop.getLine(),
            functionContext.expression().stop.getCharPositionInLine() + 1)
        );
      }

      MatchNode[] patterns = new MatchNode[functionContext.pattern().size()];
      for (int j = 0; j < functionContext.pattern().size(); j++) {
        patterns[j] = visitPattern(functionContext.pattern(j));
      }
      TupleMatchNode argPatterns = new TupleMatchNode(patterns);

      if (!functionCardinality.containsKey(functionName)) {
        functionCardinality.put(functionName, patterns.length);
      } else if (!functionCardinality.get(functionName).equals(patterns.length)) {
        throw new AbzuParseError(source,
            functionContext.NAME().getSymbol().getLine(),
            functionContext.NAME().getSymbol().getCharPositionInLine() + 1,
            functionContext.NAME().getText().length(), "Function " + functionName + " is defined using patterns of varying size.");
      }

      functionPatterns.get(functionName).add(new PatternNode(argPatterns, ctx.function(i).expression().accept(this)));
    }

    List<FunctionNode> functions = new ArrayList<>();
    for (Map.Entry<String, List<PatternNode>> pair : functionPatterns.entrySet()) {
      String functionName = pair.getKey();
      List<PatternNode> patternNodes = pair.getValue();
      int cardinality = functionCardinality.get(functionName);

      ExpressionNode[] argumentNodes = new ExpressionNode[cardinality];
      for (int j = 0; j < argumentNodes.length; j++) {
        argumentNodes[j] = new ReadArgumentNode(j);
      }

      TupleNode argsTuple = new TupleNode(argumentNodes);
      CaseNode caseNode = new CaseNode(argsTuple, patternNodes.toArray(new PatternNode[]{}));

      caseNode.addRootTag();

      FunctionNode functionNode = new FunctionNode(language, functionSourceSections.get(functionName), functionName, cardinality, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), caseNode);
      functions.add(functionNode);
    }

    for (String exportedFunction : exports.strings) {
      if (!functionCardinality.containsKey(exportedFunction)) {
        throw new AbzuParseError(source,
            ctx.KW_MODULE().getSymbol().getLine(),
            ctx.KW_MODULE().getSymbol().getCharPositionInLine() + 1,
            ctx.stop.getStopIndex() - ctx.start.getStartIndex(), "Module " + moduleFQN + " is trying to export function " + exportedFunction + " that is not defined.");
      }
    }

    return new ModuleNode(moduleFQN, exports, functions.toArray(new FunctionNode[]{}));
  }

  @Override
  public NonEmptyStringListNode visitNonEmptyListOfNames(AbzuParser.NonEmptyListOfNamesContext ctx) {
    Set<String> names = new HashSet<>();
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
  public IdentifierNode visitIdentifier(AbzuParser.IdentifierContext ctx) {
    String name = ctx.NAME().getText();
    return new IdentifierNode(language, name);
  }

  private String normalizeString(String str) {
    return str.substring(1, str.length() - 1);
  }

  @Override
  public CaseNode visitCaseExpr(AbzuParser.CaseExprContext ctx) {
    ExpressionNode expr = ctx.expression().accept(this);
    PatternNode[] patternNodes = new PatternNode[ctx.patternExpression().size()];
    for (int i = 0; i < ctx.patternExpression().size(); i++) {
      patternNodes[i] = visitPatternExpression(ctx.patternExpression(i));
    }
    return new CaseNode(expr, patternNodes);
  }

  @Override
  public PatternNode visitPatternExpression(AbzuParser.PatternExpressionContext ctx) {
    MatchNode matchExpression = visitPattern(ctx.pattern());
    ExpressionNode valueExpression = ctx.expression().accept(this);
    return new PatternNode(matchExpression, valueExpression);
  }

  @Override
  public MatchNode visitPattern(AbzuParser.PatternContext ctx) {
    if (ctx.underscore() != null) {
      return UnderscoreMatchNode.INSTANCE;
    } else if (ctx.tuplePattern() != null) {
      return visitTuplePattern(ctx.tuplePattern());
    } else if (ctx.patternValue() != null) {
      return new ValueMatchNode(ctx.patternValue().accept(this));
    } else {
      return visitSequencePattern(ctx.sequencePattern());
    }
  }

  @Override
  public MatchNode visitPatternWithoutSequence(AbzuParser.PatternWithoutSequenceContext ctx) {
    if (ctx.underscore() != null) {
      return UnderscoreMatchNode.INSTANCE;
    } else if (ctx.tuplePattern() != null) {
      return visitTuplePattern(ctx.tuplePattern());
    } else {
      return new ValueMatchNode(ctx.patternValue().accept(this));
    }
  }

  @Override
  public TupleMatchNode visitTuplePattern(AbzuParser.TuplePatternContext ctx) {
    ExpressionNode expressions[] = new ExpressionNode[ctx.pattern().size()];

    for (int i = 0; i < ctx.pattern().size(); i++) {
      expressions[i] = ctx.pattern(i).accept(this);
    }

    return new TupleMatchNode(expressions);
  }

  @Override
  public MatchNode visitSequencePattern(AbzuParser.SequencePatternContext ctx) {
    if (ctx.identifier() != null) {
      return new AsSequenceMatchNode(visitIdentifier(ctx.identifier()), visitInnerSequencePattern(ctx.innerSequencePattern()));
    } else {
      return visitInnerSequencePattern(ctx.innerSequencePattern());
    }
  }

  @Override
  public MatchNode visitInnerSequencePattern(AbzuParser.InnerSequencePatternContext ctx) {
    if (ctx.headTails() != null) {
      return visitHeadTails(ctx.headTails());
    } else {
      MatchNode[] matchNodes = new MatchNode[ctx.pattern().size()];
      for (int i = 0; i < ctx.pattern().size(); i++) {
        matchNodes[i] = visitPattern(ctx.pattern(i));
      }
      return new SequenceMatchPatternNode(matchNodes);
    }
  }

  @Override
  public HeadTailsMatchPatternNode visitHeadTails(AbzuParser.HeadTailsContext ctx) {
    MatchNode pattern = visitPatternWithoutSequence(ctx.patternWithoutSequence());
    ExpressionNode tails = ctx.tails().accept(this);

    return new HeadTailsMatchPatternNode(pattern, tails);
  }
}
