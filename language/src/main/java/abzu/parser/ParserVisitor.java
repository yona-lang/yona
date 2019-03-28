package abzu.parser;

import abzu.AbzuBaseVisitor;
import abzu.AbzuLanguage;
import abzu.AbzuParser;
import abzu.ast.ExpressionNode;
import abzu.ast.MainExpressionNode;
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
    ExpressionNode functionBodyNode = new MainExpressionNode(ctx.expression().accept(this));
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
      String functionName = ctx.apply().moduleCall().name().getText();
      return new ModuleCallNode(language, fqnNode, functionName, argNodes);
    } else if (ctx.apply().nameCall() != null) {
      SimpleIdentifierNode nameNode = new SimpleIdentifierNode(ctx.apply().nameCall().var.getText());
      String functionName = ctx.apply().nameCall().fun.getText();
      return new ModuleCallNode(language, nameNode, functionName, argNodes);
    } else {
      String functionName = ctx.apply().name().getText();
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
      return new AliasNode(ctx.moduleAlias().name().getText(), visitModule(ctx.moduleAlias().module()));
    } else if (ctx.fqnAlias() != null) {
      return new AliasNode(ctx.fqnAlias().name().getText(), visitFqn(ctx.fqnAlias().fqn()));
    } else {
      return new AliasNode(ctx.lambdaAlias().name().getText(), visitLambda(ctx.lambdaAlias().lambda()));
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
    caseNode.setIsTail();

    return new FunctionNode(language, source.createSection(
        ctx.BACKSLASH().getSymbol().getLine(),
        ctx.BACKSLASH().getSymbol().getCharPositionInLine() + 1,
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
    DictNode.Entry[] entries = new DictNode.Entry[ctx.dictKey().size()];
    for (int i = 0; i < ctx.dictKey().size(); i++) {
      AbzuParser.DictKeyContext keyCtx = ctx.dictKey(i);
      AbzuParser.DictValContext expressionCtx = ctx.dictVal(i);
      entries[i] = new DictNode.Entry(keyCtx.accept(this), expressionCtx.accept(this));
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
    Map<String, List<PatternMatchable>> functionPatterns = new HashMap<>();
    Map<String, Integer> functionCardinality = new HashMap<>();
    Map<String, SourceSection> functionSourceSections = new HashMap<>();

    String lastFunctionName = null;

    for (int i = 0; i < functionPatternsCount; i++) {
      AbzuParser.FunctionContext functionContext = ctx.function(i);

      String functionName = functionContext.name().getText();

      if (lastFunctionName != null && !lastFunctionName.equals(functionName) && functionPatterns.containsKey(functionName)) {
        throw new AbzuParseError(source,
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
        throw new AbzuParseError(source,
            functionContext.name().start.getLine(),
            functionContext.name().start.getCharPositionInLine() + 1,
            functionContext.name().getText().length(), "Function " + functionName + " is defined using patterns of varying size.");
      }

      AbzuParser.FunctionBodyContext functionBodyContext = functionContext.functionBody();
      if (functionBodyContext.bodyWithoutGuard() != null) {
        functionPatterns.get(functionName).add(new PatternNode(argPatterns, functionBodyContext.bodyWithoutGuard().expression().accept(this)));
      } else {
        for (int j = 0; j < functionBodyContext.bodyWithGuards().size(); j++) {
          AbzuParser.BodyWithGuardsContext bodyWithGuardsContext = functionBodyContext.bodyWithGuards(j);

          ExpressionNode guardExpression = bodyWithGuardsContext.guard.accept(this);
          ExpressionNode expression = bodyWithGuardsContext.expr.accept(this);

          GuardedPattern guardedPattern = new GuardedPattern(argPatterns, guardExpression, expression);

          functionPatterns.get(functionName).add(guardedPattern);
        }
      }
    }

    List<FunctionNode> functions = new ArrayList<>();
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
      caseNode.setIsTail();

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
    for (AbzuParser.NameContext text : ctx.name()) {
      names.add(text.getText());
    }
    return new NonEmptyStringListNode(names.toArray(new String[]{}));
  }

  @Override
  public FQNNode visitFqn(AbzuParser.FqnContext ctx) {
    int packagePartsCount = ctx.packageName() != null ? ctx.packageName().LOWERCASE_NAME().size() : 0;
    String[] packageParts = new String[packagePartsCount];
    for (int i = 0; i < packagePartsCount; i++) {
      packageParts[i] = ctx.packageName().LOWERCASE_NAME(i).getText();
    }
    return new FQNNode(packageParts, ctx.moduleName().getText());
  }

  @Override
  public SymbolNode visitSymbol(AbzuParser.SymbolContext ctx) {
    return new SymbolNode(ctx.name().getText());
  }

  @Override
  public IdentifierNode visitIdentifier(AbzuParser.IdentifierContext ctx) {
    String name = ctx.name().getText();
    return new IdentifierNode(language, name);
  }

  private String normalizeString(String str) {
    return str.substring(1, str.length() - 1);
  }

  @Override
  public CaseNode visitCaseExpr(AbzuParser.CaseExprContext ctx) {
    ExpressionNode expr = ctx.expression().accept(this);
    List<PatternMatchable> patternNodes = new ArrayList<>();

    for (int i = 0; i < ctx.patternExpression().size(); i++) {
      AbzuParser.PatternExpressionContext patternExpressionContext = ctx.patternExpression(i);

      MatchNode matchExpression = visitPattern(patternExpressionContext.pattern());

      if (patternExpressionContext.patternExpressionWithoutGuard() != null) {
        patternNodes.add(new PatternNode(matchExpression, patternExpressionContext.patternExpressionWithoutGuard().expression().accept(this)));
      } else {
        for (int j = 0; j < patternExpressionContext.patternExpressionWithGuard().size(); j++) {
          AbzuParser.PatternExpressionWithGuardContext withGuardContext = patternExpressionContext.patternExpressionWithGuard(j);

          ExpressionNode guardExpression = withGuardContext.guard.accept(this);
          ExpressionNode expression = withGuardContext.expr.accept(this);

          GuardedPattern guardedPattern = new GuardedPattern(matchExpression, guardExpression, expression);

          patternNodes.add(guardedPattern);
        }
      }
    }

    return new CaseNode(expr, patternNodes.toArray(new PatternMatchable[]{}));
  }

  @Override
  public MatchNode visitPattern(AbzuParser.PatternContext ctx) {
    if (ctx.underscore() != null) {
      return UnderscoreMatchNode.INSTANCE;
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
  public MatchNode visitPatternWithoutSequence(AbzuParser.PatternWithoutSequenceContext ctx) {
    if (ctx.underscore() != null) {
      return UnderscoreMatchNode.INSTANCE;
    } else if (ctx.tuplePattern() != null) {
      return visitTuplePattern(ctx.tuplePattern());
    } else if (ctx.dictPattern() != null) {
      return visitDictPattern(ctx.dictPattern());
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
    } else if (ctx.tailsHead() != null) {
      return visitTailsHead(ctx.tailsHead());
    } else if (ctx.headTailsHead() != null) {
      return visitHeadTailsHead(ctx.headTailsHead());
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
    MatchNode[] headPatterns = new MatchNode[ctx.patternWithoutSequence().size()];

    for (int i = 0; i < ctx.patternWithoutSequence().size(); i++) {
      headPatterns[i] = visitPatternWithoutSequence(ctx.patternWithoutSequence(i));
    }

    ExpressionNode tails = ctx.tails().accept(this);

    return new HeadTailsMatchPatternNode(headPatterns, tails);
  }

  @Override
  public TailsHeadMatchPatternNode visitTailsHead(AbzuParser.TailsHeadContext ctx) {
    MatchNode[] headPatterns = new MatchNode[ctx.patternWithoutSequence().size()];

    for (int i = 0; i < ctx.patternWithoutSequence().size(); i++) {
      headPatterns[i] = visitPatternWithoutSequence(ctx.patternWithoutSequence(i));
    }

    ExpressionNode tails = ctx.tails().accept(this);

    return new TailsHeadMatchPatternNode(tails, headPatterns);
  }

  @Override
  public HeadTailsHeadPatternNode visitHeadTailsHead(AbzuParser.HeadTailsHeadContext ctx) {
    MatchNode[] leftPatterns = new MatchNode[ctx.leftPattern().size()];
    MatchNode[] rightPatterns = new MatchNode[ctx.rightPattern().size()];

    ExpressionNode tails = ctx.tails().accept(this);

    for (int i = 0; i < ctx.leftPattern().size(); i++) {
      leftPatterns[i] = visitPatternWithoutSequence(ctx.leftPattern(i).patternWithoutSequence());
    }

    for (int i = 0; i < ctx.rightPattern().size(); i++) {
      rightPatterns[i] = visitPatternWithoutSequence(ctx.rightPattern(i).patternWithoutSequence());
    }

    return new HeadTailsHeadPatternNode(leftPatterns, tails, rightPatterns);
  }

  @Override
  public DictMatchNode visitDictPattern(AbzuParser.DictPatternContext ctx) {
    int cnt = ctx.patternValue().size();
    ExpressionNode[] expressionNodes = new ExpressionNode[cnt];
    MatchNode[] matchNodes = new MatchNode[cnt];

    for (int i = 0; i < cnt; i++) {
      expressionNodes[i] = ctx.patternValue(i).accept(this);
      matchNodes[i] = visitPattern(ctx.pattern(i));
    }

    return new DictMatchNode(expressionNodes, matchNodes);
  }

  @Override
  public LetNode visitImportExpr(AbzuParser.ImportExprContext ctx) {
    List<AliasNode> aliasNodes = new ArrayList<>();

    for (int i = 0; i < ctx.importClause().size(); i++) {
      AbzuParser.ImportClauseContext importClauseContext = ctx.importClause(i);

      if (importClauseContext.moduleImport() != null) {
        aliasNodes.add(visitModuleImport(importClauseContext.moduleImport()));
      } else {
        FQNNode fqnNode = visitFqn(importClauseContext.functionsImport().fqn());
        for (AbzuParser.FunctionAliasContext nameContext : importClauseContext.functionsImport().functionAlias()) {
          String functionName = nameContext.funName.getText();
          String functionAlias = nameContext.funAlias != null ? nameContext.funAlias.getText() : functionName;
          aliasNodes.add(new AliasNode(functionAlias, new FunctionIdentifierNode(fqnNode, functionName)));
        }
      }
    }

    ExpressionNode expressionNode = ctx.expression().accept(this);
    return new LetNode(aliasNodes.toArray(new AliasNode[]{}), expressionNode);
  }

  @Override
  public AliasNode visitModuleImport(AbzuParser.ModuleImportContext ctx) {
    FQNNode fqnNode = visitFqn(ctx.fqn());
    if (ctx.name() == null) {
      return new AliasNode(fqnNode.moduleName, fqnNode);
    } else {
      return new AliasNode(ctx.name().getText(), fqnNode);
    }
  }
}
