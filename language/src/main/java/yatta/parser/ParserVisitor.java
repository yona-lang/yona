package yatta.parser;

import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import org.antlr.v4.runtime.ParserRuleContext;
import yatta.YattaLanguage;
import yatta.YattaParser;
import yatta.YattaParserBaseVisitor;
import yatta.ast.ExpressionNode;
import yatta.ast.MainExpressionNode;
import yatta.ast.StringPartsNode;
import yatta.ast.binary.*;
import yatta.ast.call.InvokeNode;
import yatta.ast.call.ModuleCallNode;
import yatta.ast.controlflow.PipeLeftNode;
import yatta.ast.controlflow.PipeRightNode;
import yatta.ast.expression.*;
import yatta.ast.expression.value.*;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.pattern.*;
import yatta.runtime.UninitializedFrameSlot;

import java.util.*;

public final class ParserVisitor extends YattaParserBaseVisitor<ExpressionNode> {
  private YattaLanguage language;
  private Source source;
  private int lambdaCount = 0;
  private Stack<FQNNode> moduleStack;

  public ParserVisitor(YattaLanguage language, Source source) {
    this.language = language;
    this.source = source;
    this.moduleStack = new Stack<>();
  }

  @Override
  public ExpressionNode visitInput(YattaParser.InputContext ctx) {
    ExpressionNode functionBodyNode = new MainExpressionNode(language, ctx.expression().accept(this), moduleStack.toArray(new FQNNode[]{}));
    functionBodyNode.addRootTag();

    ModuleFunctionNode mainFunctionNode = withSourceSection(ctx, new ModuleFunctionNode(language, source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), "$main", 0, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), functionBodyNode));
    return new InvokeNode(language, mainFunctionNode, new ExpressionNode[]{}, moduleStack.toArray(new FQNNode[] {}));
  }

  @Override
  public ExpressionNode visitExpressionInParents(YattaParser.ExpressionInParentsContext ctx) {
    return ctx.expression().accept(this);
  }

  @Override
  public ExpressionNode visitNegation(YattaParser.NegationContext ctx) {
    if (ctx.OP_LOGIC_NOT() != null) {
      return withSourceSection(ctx, new NegationNode(ctx.expression().accept(this)));
    } else {
      return withSourceSection(ctx, new BinaryNegationNode(ctx.expression().accept(this)));
    }
  }

  @Override
  public ExpressionNode visitFunctionApplicationExpression(YattaParser.FunctionApplicationExpressionContext ctx) {
    ExpressionNode[] argNodes = new ExpressionNode[ctx.apply().funArg().size()];
    for (int i = 0; i < ctx.apply().funArg().size(); i++) {
      argNodes[i] = ctx.apply().funArg(i).accept(this);
    }

    YattaParser.CallContext callCtx = ctx.apply().call();
    return createCallNode(ctx, argNodes, callCtx);
  }

  @Override
  public ExpressionNode visitFunArg(YattaParser.FunArgContext ctx) {
    if (ctx.value() != null) {
      return ctx.value().accept(this);
    } else {
      return ctx.expression().accept(this);
    }
  }

  @Override
  public ExpressionNode visitCharacterLiteral(YattaParser.CharacterLiteralContext ctx) {
    return new CharacterNode(ctx.CHARACTER_LITERAL().getText().codePointAt(1));
  }

  @Override
  public ConditionNode visitConditionalExpression(YattaParser.ConditionalExpressionContext ctx) {
    ExpressionNode ifNode = ctx.conditional().ifX.accept(this);
    ExpressionNode thenNode = ctx.conditional().thenX.accept(this);
    ExpressionNode elseNode = ctx.conditional().elseX.accept(this);
    return withSourceSection(ctx, new ConditionNode(ifNode, thenNode, elseNode));
  }

  @Override
  public ExpressionNode visitAdditiveExpression(YattaParser.AdditiveExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    switch (ctx.op.getText()) {
      case "+": return withSourceSection(ctx, PlusNodeGen.create(args));
      case "-": return withSourceSection(ctx, MinusNodeGen.create(args));
      default:
        throw new ParseError(source,
            ctx.op.getLine(),
            ctx.op.getCharPositionInLine(),
            ctx.op.getText().length(),
            "Binary operation '" + ctx.op.getText() + "' not supported");
    }
  }

  @Override
  public ExpressionNode visitBinaryShiftExpression(YattaParser.BinaryShiftExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    switch (ctx.op.getText()) {
      case "<<": return withSourceSection(ctx, LeftShiftNodeGen.create(args));
      case ">>": return withSourceSection(ctx, RightShiftNodeGen.create(args));
      case ">>>": return withSourceSection(ctx, ZerofillRightShiftNodeGen.create(args));
      default:
        throw new ParseError(source,
            ctx.op.getLine(),
            ctx.op.getCharPositionInLine(),
            ctx.op.getText().length(),
            "Binary operation '" + ctx.op.getText() + "' not supported");
    }
  }

  @Override
  public ExpressionNode visitMultiplicativeExpression(YattaParser.MultiplicativeExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    switch (ctx.op.getText()) {
      case "*": return withSourceSection(ctx, MultiplyNodeGen.create(args));
      case "/": return withSourceSection(ctx, DivideNodeGen.create(args));
      case "%": return withSourceSection(ctx, ModuloNodeGen.create(args));
      default:
        throw new ParseError(source,
            ctx.op.getLine(),
            ctx.op.getCharPositionInLine(),
            ctx.op.getText().length(),
            "Binary operation '" + ctx.op.getText() + "' not supported");
    }
  }

  @Override
  public ExpressionNode visitComparativeExpression(YattaParser.ComparativeExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    switch (ctx.op.getText()) {
      case "==": return withSourceSection(ctx, EqualsNodeGen.create(args));
      case "!=": return withSourceSection(ctx, NotEqualsNodeGen.create(args));
      case "<=": return withSourceSection(ctx, LowerThanOrEqualsNodeGen.create(args));
      case "<": return withSourceSection(ctx, LowerThanNodeGen.create(args));
      case ">=": return withSourceSection(ctx, GreaterThanOrEqualsNodeGen.create(args));
      case ">": return withSourceSection(ctx, GreaterThanNodeGen.create(args));
      default:
        throw new ParseError(source,
            ctx.op.getLine(),
            ctx.op.getCharPositionInLine(),
            ctx.op.getText().length(),
            "Binary operation '" + ctx.op.getText() + "' not supported");
    }
  }

  @Override
  public ExpressionNode visitBitwiseXorExpression(YattaParser.BitwiseXorExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, BitwiseXorNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitBitwiseOrExpression(YattaParser.BitwiseOrExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, BitwiseOrNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitBitwiseAndExpression(YattaParser.BitwiseAndExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, BitwiseAndNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitLogicalOrExpression(YattaParser.LogicalOrExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, LogicalOrNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitLogicalAndExpression(YattaParser.LogicalAndExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, LogicalAndNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitConsLeftExpression(YattaParser.ConsLeftExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, SequenceLeftConsNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitConsRightExpression(YattaParser.ConsRightExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, SequenceRightConsNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitJoinExpression(YattaParser.JoinExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, JoinNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitDifferenceExpression(YattaParser.DifferenceExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, DifferenceNodeGen.create(args));
  }

  @Override
  public ExpressionNode visitInExpression(YattaParser.InExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));
    ExpressionNode[] args = new ExpressionNode[]{left, right};

    return withSourceSection(ctx, InNodeGen.create(args));
  }

  @Override
  public PatternLetNode visitLetExpression(YattaParser.LetExpressionContext ctx) {
    ExpressionNode[] aliasNodes = new ExpressionNode[ctx.let().alias().size()];

    for (int i = 0; i < ctx.let().alias().size(); i++) {
      aliasNodes[i] = visitAlias(ctx.let().alias(i));
    }

    return withSourceSection(ctx, new PatternLetNode(aliasNodes, ctx.let().expression().accept(this)));
  }

  @Override
  public ExpressionNode visitAlias(YattaParser.AliasContext ctx) {
    if (ctx.patternAlias() != null) {
      return withSourceSection(ctx, new PatternAliasNode(visitPattern(ctx.patternAlias().pattern()), ctx.patternAlias().expression().accept(this)));
    } else if (ctx.moduleAlias() != null) {
      return withSourceSection(ctx, new AliasNode(ctx.moduleAlias().name().getText(), visitModule(ctx.moduleAlias().module())));
    } else if (ctx.fqnAlias() != null) {
      return withSourceSection(ctx, new AliasNode(ctx.fqnAlias().name().getText(), visitFqn(ctx.fqnAlias().fqn())));
    } else {
      return withSourceSection(ctx, new AliasNode(ctx.lambdaAlias().name().getText(), visitLambda(ctx.lambdaAlias().lambda())));
    }
  }

  @Override
  public UnitNode visitUnit(YattaParser.UnitContext ctx) {
    return withSourceSection(ctx, new UnitNode());
  }

  @Override
  public UnderscoreMatchNode visitUnderscore(YattaParser.UnderscoreContext ctx) {
    return withSourceSection(ctx, new UnderscoreMatchNode());
  }

  @Override
  public IntegerNode visitIntegerLiteral(YattaParser.IntegerLiteralContext ctx) {
    return withSourceSection(ctx, new IntegerNode(Long.parseLong(ctx.INTEGER().getText())));
  }

  @Override
  public FloatNode visitFloatLiteral(YattaParser.FloatLiteralContext ctx) {
    String text = ctx.FLOAT() != null ? ctx.FLOAT().getText() : ctx.FLOAT_INTEGER().getText().substring(0, ctx.FLOAT_INTEGER().getText().length() - 1);
    return withSourceSection(ctx, new FloatNode(Double.parseDouble(text)));
  }

  @Override
  public ByteNode visitByteLiteral(YattaParser.ByteLiteralContext ctx) {
    String text = ctx.BYTE().getText();
    return withSourceSection(ctx, new ByteNode(Byte.parseByte(text.substring(0, text.length() - 1))));
  }

  @Override
  public ExpressionNode visitStringLiteral(YattaParser.StringLiteralContext ctx) {
    ExpressionNode[] expressionNodes = new ExpressionNode[ctx.interpolatedStringPart().size()];

    for (int i = 0; i < ctx.interpolatedStringPart().size(); i++) {
      expressionNodes[i] = ctx.interpolatedStringPart(i).accept(this);
    }

    return new StringPartsNode(expressionNodes);
  }

  @Override
  public ExpressionNode visitInterpolatedStringPart(YattaParser.InterpolatedStringPartContext ctx) {
    if (ctx.interpolatedStringExpression() != null) {
      return visitInterpolatedStringExpression(ctx.interpolatedStringExpression());
    } else if (ctx.DOUBLE_CURLY_INSIDE() != null) {
      return withSourceSection(ctx, new StringNode("{"));
    } else if (ctx.REGULAR_CHAR_INSIDE() != null) {
      return withSourceSection(ctx, new StringNode(ctx.REGULAR_CHAR_INSIDE().getText()));
    } else {
      return withSourceSection(ctx, new StringNode(ctx.REGULAR_STRING_INSIDE().getText()));
    }
  }

  @Override
  public ExpressionNode visitInterpolatedStringExpression(YattaParser.InterpolatedStringExpressionContext ctx) {
    ExpressionNode expressionNode = ctx.interpolationExpression.accept(this);
    ExpressionNode alignmentNode = (ctx.alignment != null) ? ctx.alignment.accept(this) : null;

    return new StringInterpolationNode(expressionNode, alignmentNode);
  }

  @Override
  public BooleanNode visitBooleanLiteral(YattaParser.BooleanLiteralContext ctx) {
    return withSourceSection(ctx, ctx.KW_TRUE() != null ? new BooleanNode(true) : new BooleanNode(false));
  }

  @Override
  public FunctionNode visitLambda(YattaParser.LambdaContext ctx) {
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
    caseNode.setIsTail(true);

    return withSourceSection(ctx, new FunctionNode(language, source.createSection(
        ctx.BACKSLASH().getSymbol().getLine(),
        ctx.BACKSLASH().getSymbol().getCharPositionInLine() + 1,
        ctx.expression().stop.getLine(),
        ctx.expression().stop.getCharPositionInLine() + 1
    ), "$lambda-" + lambdaCount++, ctx.pattern().size(), new FrameDescriptor(UninitializedFrameSlot.INSTANCE), caseNode));
  }

  @Override
  public TupleNode visitTuple(YattaParser.TupleContext ctx) {
    int elementsCount = ctx.expression().size();
    ExpressionNode[] content = new ExpressionNode[elementsCount];
    for (int i = 0; i < elementsCount; i++) {
      content[i] = ctx.expression(i).accept(this);
    }
    return withSourceSection(ctx, new TupleNode(content));
  }

  @Override
  public DictNode visitDict(YattaParser.DictContext ctx) {
    DictNode.Entry[] entries = new DictNode.Entry[ctx.dictKey().size()];
    for (int i = 0; i < ctx.dictKey().size(); i++) {
      YattaParser.DictKeyContext keyCtx = ctx.dictKey(i);
      YattaParser.DictValContext expressionCtx = ctx.dictVal(i);
      entries[i] = new DictNode.Entry(keyCtx.accept(this), expressionCtx.accept(this));
    }
    return withSourceSection(ctx, new DictNode(entries));
  }

  @Override
  public EmptySequenceNode visitEmptySequence(YattaParser.EmptySequenceContext ctx) {
    return withSourceSection(ctx, new EmptySequenceNode());
  }

  @Override
  public SequenceNode visitOtherSequence(YattaParser.OtherSequenceContext ctx) {
    ExpressionNode[] expressionNodes = new ExpressionNode[ctx.expression().size()];
    for (int i = 0; i < ctx.expression().size(); i++) {
      expressionNodes[i] = ctx.expression(i).accept(this);
    }
    return withSourceSection(ctx, new SequenceNode(expressionNodes));
  }

  @Override
  public ModuleNode visitModule(YattaParser.ModuleContext ctx) {
    FQNNode moduleFQN = visitFqn(ctx.fqn());
    moduleStack.push(moduleFQN);
    NonEmptyStringListNode exports = visitNonEmptyListOfNames(ctx.nonEmptyListOfNames());

    int functionPatternsCount = ctx.function().size();
    Map<String, List<PatternMatchable>> functionPatterns = new HashMap<>();
    Map<String, Integer> functionCardinality = new HashMap<>();
    Map<String, SourceSection> functionSourceSections = new HashMap<>();

    String lastFunctionName = null;

    for (int i = 0; i < functionPatternsCount; i++) {
      YattaParser.FunctionContext functionContext = ctx.function(i);

      String functionName = functionContext.name().getText();

      if (lastFunctionName != null && !lastFunctionName.equals(functionName) && functionPatterns.containsKey(functionName)) {
        throw new ParseError(source,
            functionContext.name().start.getLine(),
            functionContext.name().start.getCharPositionInLine() + 1,
            functionContext.name().getText().length(), "Function " + functionName + " was already defined previously.");
      }
      lastFunctionName = functionName;

      if (!functionPatterns.containsKey(functionName)) {
        functionPatterns.put(functionName, new ArrayList<>());
      }

      if (!functionSourceSections.containsKey(functionName)) {
        functionSourceSections.put(functionName, source.createSection(
            functionContext.name().start.getLine(),
            functionContext.name().start.getCharPositionInLine() + 1,
            functionContext.functionBody().stop.getLine(),
            functionContext.functionBody().stop.getCharPositionInLine() + 1)
        );
      } else {
        SourceSection sourceSection = functionSourceSections.get(functionName);
        functionSourceSections.put(functionName, source.createSection(
            sourceSection.getStartLine(),
            sourceSection.getStartColumn(),
            functionContext.functionBody().stop.getLine(),
            functionContext.functionBody().stop.getCharPositionInLine() + 1)
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
        throw new ParseError(source,
            functionContext.name().start.getLine(),
            functionContext.name().start.getCharPositionInLine() + 1,
            functionContext.name().getText().length(), "Function " + functionName + " is defined using patterns of varying size.");
      }

      YattaParser.FunctionBodyContext functionBodyContext = functionContext.functionBody();
      if (functionBodyContext.bodyWithoutGuard() != null) {
        functionPatterns.get(functionName).add(new PatternNode(argPatterns, functionBodyContext.bodyWithoutGuard().expression().accept(this)));
      } else {
        for (int j = 0; j < functionBodyContext.bodyWithGuards().size(); j++) {
          YattaParser.BodyWithGuardsContext bodyWithGuardsContext = functionBodyContext.bodyWithGuards(j);

          ExpressionNode guardExpression = bodyWithGuardsContext.guard.accept(this);
          ExpressionNode expression = bodyWithGuardsContext.expr.accept(this);

          GuardedPattern guardedPattern = withSourceSection(bodyWithGuardsContext, new GuardedPattern(argPatterns, guardExpression, expression));

          functionPatterns.get(functionName).add(guardedPattern);
        }
      }
    }

    List<FunctionLikeNode> functions = new ArrayList<>();
    for (Map.Entry<String, List<PatternMatchable>> pair : functionPatterns.entrySet()) {
      String functionName = pair.getKey();
      List<PatternMatchable> patternNodes = pair.getValue();
      int cardinality = functionCardinality.get(functionName);

      ExpressionNode[] argumentNodes = new ExpressionNode[cardinality];
      for (int j = 0; j < argumentNodes.length; j++) {
        argumentNodes[j] = new ReadArgumentNode(j);
      }

      TupleNode argsTuple = new TupleNode(argumentNodes);
      CaseNode caseNode = new CaseNode(argsTuple, patternNodes.toArray(new PatternMatchable[]{}));

      caseNode.addRootTag();
      caseNode.setIsTail(true);

      FunctionNode functionNode = new FunctionNode(language, functionSourceSections.get(functionName), functionName, cardinality, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), caseNode);
      functions.add(functionNode);
    }

    for (String exportedFunction : exports.strings) {
      if (!functionCardinality.containsKey(exportedFunction)) {
        throw new ParseError(source,
            ctx.KW_MODULE().getSymbol().getLine(),
            ctx.KW_MODULE().getSymbol().getCharPositionInLine() + 1,
            ctx.stop.getStopIndex() - ctx.start.getStartIndex(), "Module " + moduleFQN + " is trying to export function " + exportedFunction + " that is not defined.");
      }
    }

    moduleStack.pop();
    return withSourceSection(ctx, new ModuleNode(moduleFQN, exports, functions.toArray(new FunctionLikeNode[]{})));
  }

  @Override
  public NonEmptyStringListNode visitNonEmptyListOfNames(YattaParser.NonEmptyListOfNamesContext ctx) {
    Set<String> names = new HashSet<>();
    for (YattaParser.NameContext text : ctx.name()) {
      names.add(text.getText());
    }
    return withSourceSection(ctx, new NonEmptyStringListNode(names.toArray(new String[]{})));
  }

  @Override
  public FQNNode visitFqn(YattaParser.FqnContext ctx) {
    int packagePartsCount = ctx.packageName() != null ? ctx.packageName().LOWERCASE_NAME().size() : 0;
    String[] packageParts = new String[packagePartsCount];
    for (int i = 0; i < packagePartsCount; i++) {
      packageParts[i] = ctx.packageName().LOWERCASE_NAME(i).getText();
    }
    return withSourceSection(ctx, new FQNNode(packageParts, ctx.moduleName().getText()));
  }

  @Override
  public SymbolNode visitSymbol(YattaParser.SymbolContext ctx) {
    return withSourceSection(ctx, new SymbolNode(ctx.name().getText()));
  }

  @Override
  public IdentifierNode visitIdentifier(YattaParser.IdentifierContext ctx) {
    String name = ctx.name().getText();
    return withSourceSection(ctx, new IdentifierNode(language, name, moduleStack.toArray(new FQNNode[] {})));
  }

  @Override
  public CaseNode visitCaseExpr(YattaParser.CaseExprContext ctx) {
    ExpressionNode expr = ctx.expression().accept(this);
    List<PatternMatchable> patternNodes = new ArrayList<>();

    for (int i = 0; i < ctx.patternExpression().size(); i++) {
      YattaParser.PatternExpressionContext patternExpressionContext = ctx.patternExpression(i);

      MatchNode matchExpression = visitPattern(patternExpressionContext.pattern());

      if (patternExpressionContext.patternExpressionWithoutGuard() != null) {
        patternNodes.add(withSourceSection(patternExpressionContext.patternExpressionWithoutGuard(), new PatternNode(matchExpression, patternExpressionContext.patternExpressionWithoutGuard().expression().accept(this))));
      } else {
        for (int j = 0; j < patternExpressionContext.patternExpressionWithGuard().size(); j++) {
          YattaParser.PatternExpressionWithGuardContext withGuardContext = patternExpressionContext.patternExpressionWithGuard(j);

          ExpressionNode guardExpression = withGuardContext.guard.accept(this);
          ExpressionNode expression = withGuardContext.expr.accept(this);

          GuardedPattern guardedPattern = withSourceSection(withGuardContext, new GuardedPattern(matchExpression, guardExpression, expression));

          patternNodes.add(guardedPattern);
        }
      }
    }

    return withSourceSection(ctx, new CaseNode(expr, patternNodes.toArray(new PatternMatchable[]{})));
  }

  @Override
  public MatchNode visitPattern(YattaParser.PatternContext ctx) {
    if (ctx.underscore() != null) {
      return withSourceSection(ctx.underscore(), new UnderscoreMatchNode());
    } else if (ctx.tuplePattern() != null) {
      return visitTuplePattern(ctx.tuplePattern());
    } else if (ctx.patternValue() != null) {
      return new ValueMatchNode(ctx.patternValue().accept(this));
    } else if (ctx.dictPattern() != null) {
      return visitDictPattern(ctx.dictPattern());
    } else {
      return visitSequencePattern(ctx.sequencePattern());
    }
  }

  @Override
  public MatchNode visitPatternWithoutSequence(YattaParser.PatternWithoutSequenceContext ctx) {
    if (ctx.underscore() != null) {
      return withSourceSection(ctx.underscore(), new UnderscoreMatchNode());
    } else if (ctx.tuplePattern() != null) {
      return visitTuplePattern(ctx.tuplePattern());
    } else if (ctx.dictPattern() != null) {
      return visitDictPattern(ctx.dictPattern());
    } else {
      return withSourceSection(ctx, new ValueMatchNode(ctx.patternValue().accept(this)));
    }
  }

  @Override
  public TupleMatchNode visitTuplePattern(YattaParser.TuplePatternContext ctx) {
    ExpressionNode expressions[] = new ExpressionNode[ctx.pattern().size()];

    for (int i = 0; i < ctx.pattern().size(); i++) {
      expressions[i] = ctx.pattern(i).accept(this);
    }

    return withSourceSection(ctx, new TupleMatchNode(expressions));
  }

  @Override
  public MatchNode visitSequencePattern(YattaParser.SequencePatternContext ctx) {
    if (ctx.identifier() != null) {
      return withSourceSection(ctx, new AsSequenceMatchNode(visitIdentifier(ctx.identifier()), visitInnerSequencePattern(ctx.innerSequencePattern())));
    } else {
      return visitInnerSequencePattern(ctx.innerSequencePattern());
    }
  }

  @Override
  public MatchNode visitInnerSequencePattern(YattaParser.InnerSequencePatternContext ctx) {
    if (ctx.headTails() != null) {
      return visitHeadTails(ctx.headTails());
    } else if (ctx.tailsHead() != null) {
      return visitTailsHead(ctx.tailsHead());
    } else if (ctx.headTailsHead() != null) {
      return visitHeadTailsHead(ctx.headTailsHead());
    } else {
      MatchNode[] matchNodes = new MatchNode[ctx.pattern().size()];
      for (int i = 0; i < ctx.pattern().size(); i++) {
        matchNodes[i] = visitPattern(ctx.pattern(i));
      }
      return withSourceSection(ctx, new SequenceMatchPatternNode(matchNodes));
    }
  }

  @Override
  public HeadTailsMatchPatternNode visitHeadTails(YattaParser.HeadTailsContext ctx) {
    MatchNode[] headPatterns = new MatchNode[ctx.patternWithoutSequence().size()];

    for (int i = 0; i < ctx.patternWithoutSequence().size(); i++) {
      headPatterns[i] = visitPatternWithoutSequence(ctx.patternWithoutSequence(i));
    }

    ExpressionNode tails = ctx.tails().accept(this);

    return withSourceSection(ctx, new HeadTailsMatchPatternNode(headPatterns, tails));
  }

  @Override
  public TailsHeadMatchPatternNode visitTailsHead(YattaParser.TailsHeadContext ctx) {
    MatchNode[] headPatterns = new MatchNode[ctx.patternWithoutSequence().size()];

    for (int i = 0; i < ctx.patternWithoutSequence().size(); i++) {
      headPatterns[i] = visitPatternWithoutSequence(ctx.patternWithoutSequence(i));
    }

    ExpressionNode tails = ctx.tails().accept(this);

    return withSourceSection(ctx, new TailsHeadMatchPatternNode(tails, headPatterns));
  }

  @Override
  public HeadTailsHeadPatternNode visitHeadTailsHead(YattaParser.HeadTailsHeadContext ctx) {
    MatchNode[] leftPatterns = new MatchNode[ctx.leftPattern().size()];
    MatchNode[] rightPatterns = new MatchNode[ctx.rightPattern().size()];

    ExpressionNode tails = ctx.tails().accept(this);

    for (int i = 0; i < ctx.leftPattern().size(); i++) {
      leftPatterns[i] = visitPatternWithoutSequence(ctx.leftPattern(i).patternWithoutSequence());
    }

    for (int i = 0; i < ctx.rightPattern().size(); i++) {
      rightPatterns[i] = visitPatternWithoutSequence(ctx.rightPattern(i).patternWithoutSequence());
    }

    return withSourceSection(ctx, new HeadTailsHeadPatternNode(leftPatterns, tails, rightPatterns));
  }

  @Override
  public DictMatchNode visitDictPattern(YattaParser.DictPatternContext ctx) {
    int cnt = ctx.patternValue().size();
    ExpressionNode[] expressionNodes = new ExpressionNode[cnt];
    MatchNode[] matchNodes = new MatchNode[cnt];

    for (int i = 0; i < cnt; i++) {
      expressionNodes[i] = ctx.patternValue(i).accept(this);
      matchNodes[i] = visitPattern(ctx.pattern(i));
    }

    return withSourceSection(ctx, new DictMatchNode(expressionNodes, matchNodes));
  }

  @Override
  public LetNode visitImportExpr(YattaParser.ImportExprContext ctx) {
    List<AliasNode> aliasNodes = new ArrayList<>();

    for (int i = 0; i < ctx.importClause().size(); i++) {
      YattaParser.ImportClauseContext importClauseContext = ctx.importClause(i);

      if (importClauseContext.moduleImport() != null) {
        aliasNodes.add(visitModuleImport(importClauseContext.moduleImport()));
      } else {
        FQNNode fqnNode = visitFqn(importClauseContext.functionsImport().fqn());
        for (YattaParser.FunctionAliasContext nameContext : importClauseContext.functionsImport().functionAlias()) {
          String functionName = nameContext.funName.getText();
          String functionAlias = nameContext.funAlias != null ? nameContext.funAlias.getText() : functionName;
          aliasNodes.add(withSourceSection(nameContext, new AliasNode(functionAlias, new FunctionIdentifierNode(fqnNode, functionName))));
        }
      }
    }

    ExpressionNode expressionNode = ctx.expression().accept(this);
    return withSourceSection(ctx, new LetNode(aliasNodes.toArray(new AliasNode[]{}), expressionNode));
  }

  @Override
  public AliasNode visitModuleImport(YattaParser.ModuleImportContext ctx) {
    FQNNode fqnNode = visitFqn(ctx.fqn());
    if (ctx.name() == null) {
      return withSourceSection(ctx, new AliasNode(fqnNode.moduleName, fqnNode));
    } else {
      return withSourceSection(ctx, new AliasNode(ctx.name().getText(), fqnNode));
    }
  }

  @Override
  public ExpressionNode visitDoExpr(YattaParser.DoExprContext ctx) {
    ExpressionNode[] steps = new ExpressionNode[ctx.doOneStep().size()];

    for (int i = 0; i < ctx.doOneStep().size(); i++) {
      steps[i] = visitDoOneStep(ctx.doOneStep(i));
    }

    return withSourceSection(ctx, new DoNode(steps));
  }

  @Override
  public ExpressionNode visitDoOneStep(YattaParser.DoOneStepContext ctx) {
    if (ctx.alias() != null) {
      return visitAlias(ctx.alias());
    } else {
      return ctx.expression().accept(this);
    }
  }

  @Override
  public ExpressionNode visitTryCatchExpr(YattaParser.TryCatchExprContext ctx) {
    ExpressionNode tryExpression = ctx.expression().accept(this);
    List<PatternMatchable> patternNodes = new ArrayList<>();

    for (int i = 0; i < ctx.catchExpr().catchPatternExpression().size(); i++) {
      YattaParser.CatchPatternExpressionContext patternExpressionContext = ctx.catchExpr().catchPatternExpression(i);

      MatchNode matchExpression = null;

      if (patternExpressionContext.tripplePattern() != null) {
        matchExpression = visitTripplePattern(patternExpressionContext.tripplePattern());
      } else {
        matchExpression = visitUnderscore(patternExpressionContext.underscore());
      }

      if (patternExpressionContext.catchPatternExpressionWithoutGuard() != null) {
        patternNodes.add(withSourceSection(patternExpressionContext.catchPatternExpressionWithoutGuard(), new PatternNode(matchExpression, patternExpressionContext.catchPatternExpressionWithoutGuard().expression().accept(this))));
      } else {
        for (int j = 0; j < patternExpressionContext.catchPatternExpressionWithGuard().size(); j++) {
          YattaParser.CatchPatternExpressionWithGuardContext withGuardContext = patternExpressionContext.catchPatternExpressionWithGuard(j);

          ExpressionNode guardExpression = withGuardContext.guard.accept(this);
          ExpressionNode expression = withGuardContext.expr.accept(this);

          GuardedPattern guardedPattern = withSourceSection(withGuardContext, new GuardedPattern(matchExpression, guardExpression, expression));

          patternNodes.add(guardedPattern);
        }
      }
    }

    return withSourceSection(ctx, new TryCatchNode(tryExpression, patternNodes.toArray(new PatternMatchable[]{})));
  }

  @Override
  public TupleMatchNode visitTripplePattern(YattaParser.TripplePatternContext ctx) {
    ExpressionNode expressions[] = new ExpressionNode[ctx.pattern().size()];

    for (int i = 0; i < ctx.pattern().size(); i++) {
      expressions[i] = ctx.pattern(i).accept(this);
    }

    return withSourceSection(ctx, new TupleMatchNode(expressions));
  }

  @Override
  public ThrowYattaExceptionNode visitRaiseExpr(YattaParser.RaiseExprContext ctx) {
    return withSourceSection(ctx, new ThrowYattaExceptionNode(visitSymbol(ctx.symbol()), visitStringLiteral(ctx.stringLiteral())));
  }

  @Override
  public ExpressionNode visitBacktickExpression(YattaParser.BacktickExpressionContext ctx) {
    ExpressionNode[] argNodes = new ExpressionNode[2];
    argNodes[0] = ctx.left.accept(this);
    argNodes[1] = ctx.right.accept(this);

    YattaParser.CallContext callCtx = ctx.call();
    return createCallNode(ctx, argNodes, callCtx);
  }

  private ExpressionNode createCallNode(ParserRuleContext ctx, ExpressionNode[] argNodes, YattaParser.CallContext callCtx) {
    FQNNode[] moduleStackArray = moduleStack.toArray(new FQNNode[] {});
    if (callCtx.moduleCall() != null) {
      FQNNode fqnNode = visitFqn(callCtx.moduleCall().fqn());
      String functionName = callCtx.moduleCall().name().getText();
      return withSourceSection(ctx, new ModuleCallNode(language, fqnNode, functionName, argNodes, moduleStackArray));
    } else if (callCtx.nameCall() != null) {
      SimpleIdentifierNode nameNode = new SimpleIdentifierNode(callCtx.nameCall().var.getText());
      String functionName = callCtx.nameCall().fun.getText();
      return withSourceSection(ctx, new ModuleCallNode(language, nameNode, functionName, argNodes, moduleStackArray));
    } else {
      String functionName = callCtx.name().getText();
      return withSourceSection(ctx, new InvokeNode(language, withSourceSection(callCtx.name(), new IdentifierNode(language, functionName, moduleStackArray)), argNodes, moduleStackArray));
    }
  }

  @Override
  public ExpressionNode visitPipeLeftExpression(YattaParser.PipeLeftExpressionContext ctx) {
    return withSourceSection(ctx, new PipeLeftNode(language, ctx.left.accept(this), ctx.right.accept(this), moduleStack.toArray(new FQNNode[] {})));
  }

  @Override
  public ExpressionNode visitPipeRightExpression(YattaParser.PipeRightExpressionContext ctx) {
    return withSourceSection(ctx, new PipeRightNode(language, ctx.left.accept(this), ctx.right.accept(this), moduleStack.toArray(new FQNNode[] {})));
  }

  @Override
  public SetNode visitSet(YattaParser.SetContext ctx) {
    ExpressionNode[] expressionNodes = new ExpressionNode[ctx.expression().size()];
    for (int i = 0; i < ctx.expression().size(); i++) {
      expressionNodes[i] = ctx.expression(i).accept(this);
    }
    return withSourceSection(ctx, new SetNode(expressionNodes));
  }

  private <T extends ExpressionNode> T withSourceSection(ParserRuleContext parserRuleContext, T expressionNode) {
    expressionNode.setSourceSection(
        parserRuleContext.start.getStartIndex(),
        parserRuleContext.stop.getStopIndex() - parserRuleContext.start.getStartIndex()
    );
    return expressionNode;
  }
}
