package yatta.parser;

import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import org.antlr.v4.runtime.ParserRuleContext;
import yatta.YattaLanguage;
import yatta.lang.YattaParser;
import yatta.lang.YattaParserBaseVisitor;
import yatta.ast.ExpressionNode;
import yatta.ast.MainExpressionNode;
import yatta.ast.StringPartsNode;
import yatta.ast.binary.*;
import yatta.ast.call.InvokeNode;
import yatta.ast.call.ModuleCallNode;
import yatta.ast.expression.*;
import yatta.ast.expression.value.*;
import yatta.ast.generators.GeneratedCollection;
import yatta.ast.generators.GeneratorNode;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.pattern.*;
import yatta.runtime.Context;
import yatta.runtime.Dict;

import java.util.*;

public final class ParserVisitor extends YattaParserBaseVisitor<ExpressionNode> {
  private YattaLanguage language;
  private Source source;
  private int lambdaCount = 0;
  private Stack<FQNNode> moduleStack;
  private final Context context;

  public ParserVisitor(YattaLanguage language, Context context, Source source) {
    this.language = language;
    this.source = source;
    this.moduleStack = new Stack<>();
    this.context = context;
  }

  @Override
  public ExpressionNode visitInput(YattaParser.InputContext ctx) {
    ExpressionNode functionBodyNode = new MainExpressionNode(ctx.expression().accept(this));
    functionBodyNode.addRootTag();

    ModuleFunctionNode mainFunctionNode = withSourceSection(ctx, new ModuleFunctionNode(language, source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), null, "$main", 0, context.globalFrameDescriptor, functionBodyNode));
    return new InvokeNode(language, mainFunctionNode, new ExpressionNode[]{}, moduleStack.toArray(new ExpressionNode[] {}));
  }

  @Override
  public ExpressionNode visitExpressionInParents(YattaParser.ExpressionInParentsContext ctx) {
    return ctx.expression().accept(this);
  }

  @Override
  public ExpressionNode visitNegationExpression(YattaParser.NegationExpressionContext ctx) {
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
      case "**": return withSourceSection(ctx, PowerNodeGen.create(args));
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

    return withSourceSection(ctx, new LogicalOrNode(left, right));
  }

  @Override
  public ExpressionNode visitLogicalAndExpression(YattaParser.LogicalAndExpressionContext ctx) {
    ExpressionNode left = UnboxNodeGen.create(ctx.left.accept(this));
    ExpressionNode right = UnboxNodeGen.create(ctx.right.accept(this));

    return withSourceSection(ctx, new LogicalAndNode(left, right));
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
      return withSourceSection(ctx, new NameAliasNode(ctx.moduleAlias().name().getText(), visitModule(ctx.moduleAlias().module())));
    } else if (ctx.fqnAlias() != null) {
      return withSourceSection(ctx, new NameAliasNode(ctx.fqnAlias().name().getText(), visitFqn(ctx.fqnAlias().fqn())));
    } else if (ctx.valueAlias() != null) {
      return withSourceSection(ctx, new NameAliasNode(ctx.valueAlias().identifier().getText(), ctx.valueAlias().expression().accept(this)));
    } else {
      return withSourceSection(ctx, new NameAliasNode(ctx.lambdaAlias().name().getText(), visitLambda(ctx.lambdaAlias().lambda())));
    }
  }

  @Override
  public ExpressionNode visitValueAlias(YattaParser.ValueAliasContext ctx) {
    return withSourceSection(ctx, new NameAliasNode(ctx.identifier().getText(), ctx.expression().accept(this)));
  }

  @Override
  public RecordInstanceNode visitRecordInstance(YattaParser.RecordInstanceContext ctx) {
    RecordFieldValueNode[] fields = new RecordFieldValueNode[ctx.name().size()];

    for (int i = 0; i < ctx.name().size(); i++) {
      fields[i] = new RecordFieldValueNode(ctx.name(i).LOWERCASE_NAME().getText(), ctx.expression(i).accept(this));
    }

    return new RecordInstanceNode(ctx.recordType().UPPERCASE_NAME().getText(), fields, moduleStack.toArray(new ExpressionNode[] {}));
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
      return withSourceSection(ctx, new StringNode(ctx.REGULAR_STRING_INSIDE().getText().replace("}}", "}")));
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
    int argsCount = ctx.pattern().size();
    ExpressionNode bodyNode;
    if (ctx.pattern().size() > 0) {
      MatchNode[] patterns = new MatchNode[argsCount];
      for (int i = 0; i < argsCount; i++) {
        patterns[i] = visitPattern(ctx.pattern(i));
      }
      TupleMatchNode argPatterns = new TupleMatchNode(patterns);

      ExpressionNode[] argumentNodes = new ExpressionNode[argsCount];
      for (int j = 0; j < argsCount; j++) {
        argumentNodes[j] = new ReadArgumentNode(j);
      }

      TupleNode argsTuple = new TupleNode(argumentNodes);
      bodyNode = new CaseNode(argsTuple, new PatternNode[]{new PatternNode(argPatterns, ctx.expression().accept(this))});
    } else {
      bodyNode = ctx.expression().accept(this);
    }

    bodyNode.addRootTag();

    return withSourceSection(ctx, new FunctionNode(language, source.createSection(
        ctx.BACKSLASH().getSymbol().getLine(),
        ctx.BACKSLASH().getSymbol().getCharPositionInLine() + 1,
        ctx.expression().stop.getLine(),
        ctx.expression().stop.getCharPositionInLine() + 1
    ), currentModuleName(), "$lambda" + lambdaCount++ + "-" + argsCount, ctx.pattern().size(), context.globalFrameDescriptor, bodyNode));
  }

  private String currentModuleName() {
    try {
      return moduleStack.peek().moduleName;
    } catch (EmptyStackException e) {
      return null;
    }
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
    DictNode.EntryNode[] entries = new DictNode.EntryNode[ctx.dictKey().size()];
    for (int i = 0; i < ctx.dictKey().size(); i++) {
      YattaParser.DictKeyContext keyCtx = ctx.dictKey(i);
      YattaParser.DictValContext expressionCtx = ctx.dictVal(i);
      entries[i] = new DictNode.EntryNode(keyCtx.accept(this), expressionCtx.accept(this));
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
    String moduleFQNString = ctx.fqn().getText();
    moduleStack.push(moduleFQN);
    NonEmptyStringListNode exports = visitNonEmptyListOfNames(ctx.nonEmptyListOfNames());

    int functionPatternsCount = ctx.function().size();
    Map<String, List<PatternMatchable>> functionPatterns = new HashMap<>();
    Map<String, Integer> functionCardinality = new HashMap<>();
    Map<String, SourceSection> functionSourceSections = new HashMap<>();
    Dict records = Dict.empty();

    String lastFunctionName = null;

    for (int i = 0; i < ctx.record().size(); i++) {
      YattaParser.RecordContext recordContext = ctx.record(i);
      String[] fields = new String[recordContext.identifier().size()];

      for (int j = 0; j < recordContext.identifier().size(); j++) {
        fields[j] = recordContext.identifier(j).getText();
      }

      records = records.add(recordContext.UPPERCASE_NAME().getText(), fields);
    }

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

      int patternsLength = functionContext.pattern().size();

      MatchNode argPatterns;
      if (patternsLength == 0) {
        argPatterns = new UnderscoreMatchNode();
      } else {
        MatchNode[] patterns = new MatchNode[patternsLength];
        for (int j = 0; j < patternsLength; j++) {
          patterns[j] = visitPattern(functionContext.pattern(j));
        }
        argPatterns = new TupleMatchNode(patterns);
      }

      if (!functionCardinality.containsKey(functionName)) {
        functionCardinality.put(functionName, patternsLength);
      } else if (!functionCardinality.get(functionName).equals(patternsLength)) {
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

      FunctionNode functionNode = new FunctionNode(language, functionSourceSections.get(functionName), moduleFQNString, functionName, cardinality, context.globalFrameDescriptor, caseNode);
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
    return withSourceSection(ctx, new ModuleNode(moduleFQN, exports, functions.toArray(new FunctionLikeNode[]{}), records));
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
    return withSourceSection(ctx, new IdentifierNode(language, name, moduleStack.toArray(new ExpressionNode[] {})));
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
    if (ctx.pattern() != null) {
      return visitPattern(ctx.pattern());
    } else if (ctx.underscore() != null) {
        return withSourceSection(ctx.underscore(), new UnderscoreMatchNode());
    } else if (ctx.patternValue() != null) {
      return new ValueMatchNode(ctx.patternValue().accept(this));
    } else if (ctx.dataStructurePattern() != null) {
      return visitDataStructurePattern(ctx.dataStructurePattern());
    } else {
      return visitAsDataStructurePattern(ctx.asDataStructurePattern());
    }
  }

  @Override
  public MatchNode visitDataStructurePattern(YattaParser.DataStructurePatternContext ctx) {
    if (ctx.tuplePattern() != null) {
      return visitTuplePattern(ctx.tuplePattern());
    } else if (ctx.dictPattern() != null) {
      return visitDictPattern(ctx.dictPattern());
    } else if (ctx.recordPattern() != null) {
      return visitRecordPattern(ctx.recordPattern());
    } else {
      return visitSequencePattern(ctx.sequencePattern());
    }
  }

  @Override
  public MatchNode visitAsDataStructurePattern(YattaParser.AsDataStructurePatternContext ctx) {
    return withSourceSection(ctx, new AsDataStructureMatchNode(visitIdentifier(ctx.identifier()), visitDataStructurePattern(ctx.dataStructurePattern())));
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
  public MatchNode visitRecordPattern(YattaParser.RecordPatternContext ctx) {
    if (ctx.pattern().size() > 0) {
      RecordFieldsPatternNode.RecordPatternFieldNode[] fields = new RecordFieldsPatternNode.RecordPatternFieldNode[ctx.name().size()];

      for (int i = 0; i < ctx.name().size(); i++) {
        fields[i] = new RecordFieldsPatternNode.RecordPatternFieldNode(ctx.name(i).LOWERCASE_NAME().getText(), visitPattern(ctx.pattern(i)));
      }

      return withSourceSection(ctx, new RecordFieldsPatternNode(ctx.recordType().UPPERCASE_NAME().getText(), fields, moduleStack.toArray(new ExpressionNode[]{})));
    } else {
      return withSourceSection(ctx, new RecordTypePatternNode(ctx.recordType().UPPERCASE_NAME().getText(), moduleStack.toArray(new ExpressionNode[]{})));
    }
  }

  @Override
  public LetNode visitImportExpr(YattaParser.ImportExprContext ctx) {
    List<NameAliasNode> nameAliasNodes = new ArrayList<>();

    for (int i = 0; i < ctx.importClause().size(); i++) {
      YattaParser.ImportClauseContext importClauseContext = ctx.importClause(i);

      if (importClauseContext.moduleImport() != null) {
        nameAliasNodes.add(visitModuleImport(importClauseContext.moduleImport()));
      } else {
        FQNNode fqnNode = visitFqn(importClauseContext.functionsImport().fqn());
        for (YattaParser.FunctionAliasContext nameContext : importClauseContext.functionsImport().functionAlias()) {
          String functionName = nameContext.funName.getText();
          String functionAlias = nameContext.funAlias != null ? nameContext.funAlias.getText() : functionName;
          nameAliasNodes.add(withSourceSection(nameContext, new NameAliasNode(functionAlias, new FunctionIdentifierNode(fqnNode, functionName))));
        }
      }
    }

    ExpressionNode expressionNode = ctx.expression().accept(this);
    return withSourceSection(ctx, new LetNode(nameAliasNodes.toArray(new NameAliasNode[]{}), expressionNode));
  }

  @Override
  public NameAliasNode visitModuleImport(YattaParser.ModuleImportContext ctx) {
    FQNNode fqnNode = visitFqn(ctx.fqn());
    if (ctx.name() == null) {
      return withSourceSection(ctx, new NameAliasNode(fqnNode.moduleName, fqnNode));
    } else {
      return withSourceSection(ctx, new NameAliasNode(ctx.name().getText(), fqnNode));
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
    ExpressionNode[] moduleStackArray = moduleStack.toArray(new ExpressionNode[] {});
    if (callCtx.moduleCall() != null) {
      ExpressionNode nameNode;
      if (callCtx.moduleCall().fqn() != null) {
        nameNode = visitFqn(callCtx.moduleCall().fqn());
      } else {
        nameNode = withSourceSection(callCtx.moduleCall().expression(), callCtx.moduleCall().expression().accept(this));
      }
      String functionName = callCtx.moduleCall().name().getText();
      return withSourceSection(ctx, new ModuleCallNode(language, nameNode, functionName, argNodes, moduleStackArray));
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
    return withSourceSection(ctx, new InvokeNode(language, ctx.left.accept(this), new ExpressionNode[]{ctx.right.accept(this)}, moduleStack.toArray(new ExpressionNode[] {})));
  }

  @Override
  public ExpressionNode visitPipeRightExpression(YattaParser.PipeRightExpressionContext ctx) {
    return withSourceSection(ctx, new InvokeNode(language, ctx.right.accept(this), new ExpressionNode[]{ctx.left.accept(this)}, moduleStack.toArray(new ExpressionNode[] {})));
  }

  @Override
  public SetNode visitSet(YattaParser.SetContext ctx) {
    ExpressionNode[] expressionNodes = new ExpressionNode[ctx.expression().size()];
    for (int i = 0; i < ctx.expression().size(); i++) {
      expressionNodes[i] = ctx.expression(i).accept(this);
    }
    return withSourceSection(ctx, new SetNode(expressionNodes));
  }

  @Override
  public ExpressionNode visitSequenceGeneratorExpr(YattaParser.SequenceGeneratorExprContext ctx) {
    ExpressionNode reducer = ctx.reducer.accept(this);
    ExpressionNode condition = visitOptional(ctx.condition);
    MatchNode[] stepMatchNodes;
    ExpressionNode stepExpression = ctx.stepExpression.accept(this);

    if (ctx.collectionExtractor().valueCollectionExtractor() != null) {
      stepMatchNodes = new MatchNode[] {visitIdentifierOrUnderscore(ctx.collectionExtractor().valueCollectionExtractor().identifierOrUnderscore())};
    } else {
      stepMatchNodes = new MatchNode[] {
          visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().key),
          visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().val)
      };
    }

    return withSourceSection(ctx, new GeneratorNode(language, GeneratedCollection.SEQ, reducer, condition, stepMatchNodes, stepExpression, moduleStack.toArray(new ExpressionNode[] {}), currentModuleName()));
  }

  @Override
  public ExpressionNode visitSetGeneratorExpr(YattaParser.SetGeneratorExprContext ctx) {
    ExpressionNode reducer = ctx.reducer.accept(this);
    ExpressionNode condition = visitOptional(ctx.condition);
    MatchNode[] stepMatchNodes;
    ExpressionNode stepExpression = ctx.stepExpression.accept(this);

    if (ctx.collectionExtractor().valueCollectionExtractor() != null) {
      stepMatchNodes = new MatchNode[] {visitIdentifierOrUnderscore(ctx.collectionExtractor().valueCollectionExtractor().identifierOrUnderscore())};
    } else {
      stepMatchNodes = new MatchNode[] {
          visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().key),
          visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().val)
      };
    }

    return withSourceSection(ctx, new GeneratorNode(language, GeneratedCollection.SET, reducer, condition, stepMatchNodes, stepExpression, moduleStack.toArray(new ExpressionNode[] {}), currentModuleName()));
  }

  @Override
  public ExpressionNode visitDictGeneratorExpr(YattaParser.DictGeneratorExprContext ctx) {
    TupleNode reducer = visitDictGeneratorReducer(ctx.dictGeneratorReducer());
    ExpressionNode condition = visitOptional(ctx.condition);
    MatchNode[] stepMatchNodes;
    ExpressionNode stepExpression = ctx.stepExpression.accept(this);

    if (ctx.collectionExtractor().valueCollectionExtractor() != null) {
      stepMatchNodes = new MatchNode[] {visitIdentifierOrUnderscore(ctx.collectionExtractor().valueCollectionExtractor().identifierOrUnderscore())};
    } else {
      stepMatchNodes = new MatchNode[] {
          visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().key),
          visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().val)
      };
    }

    return withSourceSection(ctx, new GeneratorNode(language, GeneratedCollection.DICT, reducer, condition, stepMatchNodes, stepExpression, moduleStack.toArray(new ExpressionNode[] {}), currentModuleName()));
  }

  @Override
  public MatchNode visitIdentifierOrUnderscore(YattaParser.IdentifierOrUnderscoreContext ctx) {
    if (ctx.identifier() != null) {
      return new ValueMatchNode(visitIdentifier(ctx.identifier()));
    } else {
      return visitUnderscore(ctx.underscore());
    }
  }

  @Override
  public TupleNode visitDictGeneratorReducer(YattaParser.DictGeneratorReducerContext ctx) {
    return withSourceSection(ctx, new TupleNode(ctx.dictKey().accept(this), ctx.dictVal().accept(this)));
  }

  @Override
  public FieldAccessNode visitFieldAccessExpr(YattaParser.FieldAccessExprContext ctx) {
    return withSourceSection(ctx, new FieldAccessNode(visitIdentifier(ctx.identifier()), ctx.name().getText(), moduleStack.toArray(new ExpressionNode[] {})));
  }

  @Override
  public RecordUpdateNode visitFieldUpdateExpr(YattaParser.FieldUpdateExprContext ctx) {
    RecordFieldValueNode[] fields = new RecordFieldValueNode[ctx.name().size()];

    for (int i = 0; i < ctx.name().size(); i++) {
      fields[i] = new RecordFieldValueNode(ctx.name(i).LOWERCASE_NAME().getText(), ctx.expression(i).accept(this));
    }

    return new RecordUpdateNode(visitIdentifier(ctx.identifier()), fields, moduleStack.toArray(new ExpressionNode[] {}));
  }

  public ExpressionNode visitOptional(ParserRuleContext ctx) {
    if (ctx == null) return null;
    else return ctx.accept(this);
  }

  private <T extends ExpressionNode> T withSourceSection(ParserRuleContext parserRuleContext, T expressionNode) {
    final SourceSection sourceSection;
    sourceSection = source.createSection(parserRuleContext.start.getLine());
    expressionNode.setSourceSection(sourceSection);
    return expressionNode;
  }
}
