package yona.parser;

import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.api.source.SourceSection;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.tree.ParseTree;
import org.antlr.v4.runtime.tree.TerminalNode;
import yona.YonaLanguage;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.MainExpressionNode;
import yona.ast.StringPartsNode;
import yona.ast.binary.*;
import yona.ast.call.InvokeNode;
import yona.ast.call.ModuleCallNode;
import yona.ast.expression.*;
import yona.ast.expression.value.*;
import yona.ast.generators.GeneratedCollection;
import yona.ast.generators.GeneratorNode;
import yona.ast.local.ReadArgumentNode;
import yona.ast.pattern.*;
import yona.runtime.Context;
import yona.runtime.Dict;

import java.util.*;
import java.util.function.BiFunction;

public final class ParserVisitor extends YonaParserBaseVisitor<ExpressionNode> {
  private final YonaLanguage language;
  private final Source source;
  private int lambdaCount = 0;
  private final Deque<FQNNode> moduleStack;
  private final Context context;

  public ParserVisitor(YonaLanguage language, Context context, Source source) {
    this.language = language;
    this.source = source;
    this.moduleStack = new ArrayDeque<>();
    this.context = context;
  }

  @Override
  public ExpressionNode visitInput(YonaParser.InputContext ctx) {
    ExpressionNode functionBodyNode = new MainExpressionNode(ctx.expression().accept(this));
    functionBodyNode.addRootTag();

    ModuleFunctionNode mainFunctionNode = new ModuleFunctionNode(language, source.createSection(ctx.getSourceInterval().a, ctx.getSourceInterval().b), null, "$main", 0, context.globalFrameDescriptor, functionBodyNode);
    return new InvokeNode(language, mainFunctionNode, new ExpressionNode[]{}, moduleStack.toArray(new ExpressionNode[]{}));
  }

  @Override
  public ExpressionNode visitExpressionInParents(YonaParser.ExpressionInParentsContext ctx) {
    return ctx.expression().accept(this);
  }

  @Override
  public ExpressionNode visitNegationExpression(YonaParser.NegationExpressionContext ctx) {
    if (ctx.OP_LOGIC_NOT() != null) {
      return withSourceSection(ctx, new NegationNode(ctx.expression().accept(this)));
    } else {
      return withSourceSection(ctx, new BinaryNegationNode(ctx.expression().accept(this)));
    }
  }

  @Override
  public ExpressionNode visitFunctionApplicationExpression(YonaParser.FunctionApplicationExpressionContext ctx) {
    ExpressionNode[] argNodes = new ExpressionNode[ctx.apply().funArg().size()];
    for (int i = 0; i < ctx.apply().funArg().size(); i++) {
      argNodes[i] = ctx.apply().funArg(i).accept(this);
    }

    YonaParser.CallContext callCtx = ctx.apply().call();
    return createCallNode(ctx, argNodes, callCtx);
  }

  @Override
  public ExpressionNode visitFunArg(YonaParser.FunArgContext ctx) {
    if (ctx.value() != null) {
      return ctx.value().accept(this);
    } else {
      return ctx.expression().accept(this);
    }
  }

  @Override
  public ExpressionNode visitCharacterLiteral(YonaParser.CharacterLiteralContext ctx) {
    return new CharacterNode(ctx.CHARACTER_LITERAL().getText().codePointAt(1));
  }

  @Override
  public ConditionNode visitConditionalExpression(YonaParser.ConditionalExpressionContext ctx) {
    ExpressionNode ifNode = ctx.conditional().ifX.accept(this);
    ExpressionNode thenNode = ctx.conditional().thenX.accept(this);
    ExpressionNode elseNode = ctx.conditional().elseX.accept(this);
    return withSourceSection(ctx, new ConditionNode(ifNode, thenNode, elseNode));
  }

  @Override
  public BinaryOpNode visitAdditiveExpression(YonaParser.AdditiveExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return switch (ctx.op.getText()) {
      case "+" -> withSourceSection(ctx, newBinaryOpNode(PlusNodeGen::create, left, right));
      case "-" -> withSourceSection(ctx, newBinaryOpNode(MinusNodeGen::create, left, right));
      default -> throw new ParseError(source,
        ctx.op.getLine(),
        ctx.op.getCharPositionInLine(),
        ctx.op.getText().length(),
        "Binary operation '%s' not supported".formatted(ctx.op.getText()));
    };
  }

  @Override
  public BinaryOpNode visitBinaryShiftExpression(YonaParser.BinaryShiftExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return switch (ctx.op.getText()) {
      case "<<" -> withSourceSection(ctx, newBinaryOpNode(LeftShiftNodeGen::create, left, right));
      case ">>" -> withSourceSection(ctx, newBinaryOpNode(RightShiftNodeGen::create, left, right));
      case ">>>" -> withSourceSection(ctx, newBinaryOpNode(ZerofillRightShiftNodeGen::create, left, right));
      default -> throw new ParseError(source,
        ctx.op.getLine(),
        ctx.op.getCharPositionInLine(),
        ctx.op.getText().length(),
        "Binary operation '%s' not supported".formatted(ctx.op.getText()));
    };
  }

  @Override
  public BinaryOpNode visitMultiplicativeExpression(YonaParser.MultiplicativeExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return switch (ctx.op.getText()) {
      case "**" -> withSourceSection(ctx, newBinaryOpNode(PowerNodeGen::create, left, right));
      case "*" -> withSourceSection(ctx, newBinaryOpNode(MultiplyNodeGen::create, left, right));
      case "/" -> withSourceSection(ctx, newBinaryOpNode(DivideNodeGen::create, left, right));
      case "%" -> withSourceSection(ctx, newBinaryOpNode(ModuloNodeGen::create, left, right));
      default -> throw new ParseError(source,
        ctx.op.getLine(),
        ctx.op.getCharPositionInLine(),
        ctx.op.getText().length(),
        "Binary operation '%s' not supported".formatted(ctx.op.getText()));
    };
  }

  @Override
  public BinaryOpNode visitComparativeExpression(YonaParser.ComparativeExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return switch (ctx.op.getText()) {
      case "==" -> withSourceSection(ctx, newBinaryOpNode(EqualsNodeGen::create, left, right));
      case "!=" -> withSourceSection(ctx, newBinaryOpNode(NotEqualsNodeGen::create, left, right));
      case "<=" -> withSourceSection(ctx, newBinaryOpNode(LowerThanOrEqualsNodeGen::create, left, right));
      case "<" -> withSourceSection(ctx, newBinaryOpNode(LowerThanNodeGen::create, left, right));
      case ">=" -> withSourceSection(ctx, newBinaryOpNode(GreaterThanOrEqualsNodeGen::create, left, right));
      case ">" -> withSourceSection(ctx, newBinaryOpNode(GreaterThanNodeGen::create, left, right));
      default -> throw new ParseError(source,
        ctx.op.getLine(),
        ctx.op.getCharPositionInLine(),
        ctx.op.getText().length(),
        "Binary operation '%s' not supported".formatted(ctx.op.getText()));
    };
  }

  @Override
  public BinaryOpNode visitBitwiseXorExpression(YonaParser.BitwiseXorExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, newBinaryOpNode(BitwiseXorNodeGen::create, left, right));
  }

  @Override
  public BinaryOpNode visitBitwiseOrExpression(YonaParser.BitwiseOrExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, newBinaryOpNode(BitwiseOrNodeGen::create, left, right));
  }

  @Override
  public BinaryOpNode visitBitwiseAndExpression(YonaParser.BitwiseAndExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, newBinaryOpNode(BitwiseAndNodeGen::create, left, right));
  }

  @Override
  public ExpressionNode visitLogicalOrExpression(YonaParser.LogicalOrExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, new LogicalOrNode(left, right));
  }

  @Override
  public ExpressionNode visitLogicalAndExpression(YonaParser.LogicalAndExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, new LogicalAndNode(left, right));
  }

  @Override
  public BinaryOpNode visitConsLeftExpression(YonaParser.ConsLeftExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, newBinaryOpNode(SequenceLeftConsNodeGen::create, left, right));
  }

  @Override
  public BinaryOpNode visitConsRightExpression(YonaParser.ConsRightExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, newBinaryOpNode(SequenceRightConsNodeGen::create, left, right));
  }

  @Override
  public BinaryOpNode visitJoinExpression(YonaParser.JoinExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, newBinaryOpNode(JoinNodeGen::create, left, right));
  }

  @Override
  public BinaryOpNode visitInExpression(YonaParser.InExpressionContext ctx) {
    ExpressionNode left = newUnboxNode(ctx.left);
    ExpressionNode right = newUnboxNode(ctx.right);

    return withSourceSection(ctx, newBinaryOpNode(InNodeGen::create, left, right));
  }

  private UnboxNode newUnboxNode(YonaParser.ExpressionContext ctx) {
    ExpressionNode expressionNode = ctx.accept(this);
    return withSourceSection(ctx, UnboxNodeGen.create(expressionNode).setValue(expressionNode));
  }

  private <T extends BinaryOpNode> BinaryOpNode newBinaryOpNode(BiFunction<ExpressionNode, ExpressionNode, T> ctor, ExpressionNode left, ExpressionNode right) {
    return ctor.apply(left, right).setLeft(left).setRight(right);
  }

  @Override
  public PatternLetNode visitLetExpression(YonaParser.LetExpressionContext ctx) {
    AliasNode[] aliasNodes = new AliasNode[ctx.let().alias().size()];

    for (int i = 0; i < ctx.let().alias().size(); i++) {
      aliasNodes[i] = visitAlias(ctx.let().alias(i));
    }

    return withSourceSection(ctx, new PatternLetNode(aliasNodes, ctx.let().expression().accept(this)));
  }

  @Override
  public AliasNode visitAlias(YonaParser.AliasContext ctx) {
    if (ctx.patternAlias() != null) {
      return withSourceSection(ctx, new PatternAliasNode(visitPattern(ctx.patternAlias().pattern()), ctx.patternAlias().expression().accept(this)));
    } else if (ctx.moduleAlias() != null) {
      return withSourceSection(ctx, new PatternAliasNode(valueMatchNodeFor(ctx.moduleAlias()), visitModule(ctx.moduleAlias().module())));
    } else if (ctx.fqnAlias() != null) {
      return withSourceSection(ctx, new PatternAliasNode(valueMatchNodeFor(ctx.fqnAlias()), visitFqn(ctx.fqnAlias().fqn())));
    } else if (ctx.valueAlias() != null) {
      return withSourceSection(ctx, new PatternAliasNode(valueMatchNodeFor(ctx.valueAlias()), ctx.valueAlias().expression().accept(this)));
    } else {
      return withSourceSection(ctx, new PatternAliasNode(valueMatchNodeFor(ctx.lambdaAlias()), visitLambda(ctx.lambdaAlias().lambda())));
    }
  }

  private <T extends ParserRuleContext> MatchNode valueMatchNodeFor(T ctx) {
    return withSourceSection(ctx, new ValueMatchNode(visit(ctx)));
  }

  @Override
  public IdentifierNode visitModuleAlias(YonaParser.ModuleAliasContext ctx) {
    return withSourceSection(ctx, new IdentifierNode(language, ctx.name().getText(), moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public IdentifierNode visitFqnAlias(YonaParser.FqnAliasContext ctx) {
    return withSourceSection(ctx, new IdentifierNode(language, ctx.name().getText(), moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public IdentifierNode visitValueAlias(YonaParser.ValueAliasContext ctx) {
    return withSourceSection(ctx, new IdentifierNode(language, ctx.identifier().getText(), moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public IdentifierNode visitLambdaAlias(YonaParser.LambdaAliasContext ctx) {
    return withSourceSection(ctx, new IdentifierNode(language, ctx.name().getText(), moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public RecordInstanceNode visitRecordInstance(YonaParser.RecordInstanceContext ctx) {
    RecordFieldValueNode[] fields = new RecordFieldValueNode[ctx.name().size()];

    for (int i = 0; i < ctx.name().size(); i++) {
      fields[i] = new RecordFieldValueNode(ctx.name(i).LOWERCASE_NAME().getText(), ctx.expression(i).accept(this));
    }

    return new RecordInstanceNode(ctx.recordType().UPPERCASE_NAME().getText(), fields, moduleStack.toArray(new ExpressionNode[]{}));
  }

  @Override
  public UnitNode visitUnit(YonaParser.UnitContext ctx) {
    return withSourceSection(ctx, UnitNode.INSTANCE);
  }

  @Override
  public UnderscoreMatchNode visitUnderscore(YonaParser.UnderscoreContext ctx) {
    return withSourceSection(ctx, new UnderscoreMatchNode());
  }

  @Override
  public IntegerNode visitIntegerLiteral(YonaParser.IntegerLiteralContext ctx) {
    return withSourceSection(ctx, new IntegerNode(Long.parseLong(ctx.INTEGER().getText())));
  }

  @Override
  public FloatNode visitFloatLiteral(YonaParser.FloatLiteralContext ctx) {
    String text = ctx.FLOAT() != null ? ctx.FLOAT().getText() : ctx.FLOAT_INTEGER().getText().substring(0, ctx.FLOAT_INTEGER().getText().length() - 1);
    return withSourceSection(ctx, new FloatNode(Double.parseDouble(text)));
  }

  @Override
  public ByteNode visitByteLiteral(YonaParser.ByteLiteralContext ctx) {
    String text = ctx.BYTE().getText();
    return withSourceSection(ctx, new ByteNode(Byte.parseByte(text.substring(0, text.length() - 1))));
  }

  @Override
  public ExpressionNode visitStringLiteral(YonaParser.StringLiteralContext ctx) {
    List<ExpressionNode> expressionNodes = new ArrayList<>();

    for (ParseTree child : ctx.children) {
      if (child instanceof YonaParser.InterpolatedStringPartContext) {
        expressionNodes.add(visitInterpolatedStringPart((YonaParser.InterpolatedStringPartContext) child));
      } else if (child instanceof TerminalNode terminalNode && terminalNode.getSymbol().getType() == YonaLexer.REGULAR_STRING_INSIDE) {
        expressionNodes.add(new StringNode(child.getText()));
      }
    }

    return new StringPartsNode(expressionNodes.toArray(ExpressionNode[]::new));
  }

  @Override
  public ExpressionNode visitInterpolatedStringPart(YonaParser.InterpolatedStringPartContext ctx) {
    ExpressionNode expressionNode = ctx.interpolationExpression.accept(this);
    ExpressionNode alignmentNode = (ctx.alignment != null) ? ctx.alignment.accept(this) : null;

    return withSourceSection(ctx, new StringInterpolationNode(expressionNode, alignmentNode));
  }

  @Override
  public BooleanNode visitBooleanLiteral(YonaParser.BooleanLiteralContext ctx) {
    return withSourceSection(ctx, ctx.KW_TRUE() != null ? new BooleanNode(true) : new BooleanNode(false));
  }

  @Override
  public ExpressionNode visitLambda(YonaParser.LambdaContext ctx) {
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

    if (bodyNode instanceof LiteralValueNode) {
      return bodyNode;
    } else {
      bodyNode.addRootTag();

      return withSourceSection(ctx, new FunctionNode(language, source.createSection(
        ctx.BACKSLASH().getSymbol().getLine(),
        ctx.BACKSLASH().getSymbol().getCharPositionInLine() + 1,
        ctx.expression().stop.getLine(),
        ctx.expression().stop.getCharPositionInLine() + 1
      ), currentModuleName(), nextLambdaName() + "-" + argsCount, ctx.pattern().size(), context.globalFrameDescriptor, bodyNode));
    }
  }

  private String nextLambdaName() {
    return "$lambda" + lambdaCount++;
  }

  private String currentModuleName() {
    return !moduleStack.isEmpty() ? moduleStack.peek().moduleName : null;
  }

  @Override
  public TupleNode visitTuple(YonaParser.TupleContext ctx) {
    int elementsCount = ctx.expression().size();
    ExpressionNode[] content = new ExpressionNode[elementsCount];
    for (int i = 0; i < elementsCount; i++) {
      content[i] = ctx.expression(i).accept(this);
    }
    return withSourceSection(ctx, new TupleNode(content));
  }

  @Override
  public DictNode visitDict(YonaParser.DictContext ctx) {
    DictNode.EntryNode[] entries = new DictNode.EntryNode[ctx.dictKey().size()];
    for (int i = 0; i < ctx.dictKey().size(); i++) {
      YonaParser.DictKeyContext keyCtx = ctx.dictKey(i);
      YonaParser.DictValContext expressionCtx = ctx.dictVal(i);
      entries[i] = new DictNode.EntryNode(keyCtx.accept(this), expressionCtx.accept(this));
    }
    return withSourceSection(ctx, new DictNode(entries));
  }

  @Override
  public EmptySequenceNode visitEmptySequence(YonaParser.EmptySequenceContext ctx) {
    return withSourceSection(ctx, new EmptySequenceNode());
  }

  @Override
  public SequenceNode visitOtherSequence(YonaParser.OtherSequenceContext ctx) {
    ExpressionNode[] expressionNodes = new ExpressionNode[ctx.expression().size()];
    for (int i = 0; i < ctx.expression().size(); i++) {
      expressionNodes[i] = ctx.expression(i).accept(this);
    }
    return withSourceSection(ctx, new SequenceNode(expressionNodes));
  }

  @Override
  public ExpressionNode visitRangeSequence(YonaParser.RangeSequenceContext ctx) {
    ExpressionNode step = ctx.step != null ? withSourceSection(ctx.step, ctx.step.accept(this)) : null;
    return withSourceSection(ctx, new RangeNode(step, ctx.start.accept(this), ctx.end.accept(this)));
  }

  private interface FunctionPattern {
    FunctionLikeNode getFunctionNode(String functionName, int cardinality, String moduleFQNString, SourceSection sourceSection);
  }

  private class FunctionPatternWithArgs implements FunctionPattern {
    final List<PatternMatchable> patternNodes;

    public FunctionPatternWithArgs() {
      this.patternNodes = new ArrayList<>();
    }

    @Override
    public FunctionLikeNode getFunctionNode(String functionName, int cardinality, String moduleFQNString, SourceSection sourceSection) {
      ExpressionNode[] argumentNodes = new ExpressionNode[cardinality];
      for (int j = 0; j < argumentNodes.length; j++) {
        argumentNodes[j] = new ReadArgumentNode(j);
      }

      TupleNode argsTuple = new TupleNode(argumentNodes);
      CaseNode caseNode = new CaseNode(argsTuple, patternNodes.toArray(new PatternMatchable[]{}));

      caseNode.addRootTag();
      caseNode.setIsTail(true);

      return new FunctionNode(language, sourceSection, moduleFQNString, functionName, cardinality, context.globalFrameDescriptor, caseNode);
    }
  }

  private class FunctionPatternWithoutArgs implements FunctionPattern {
    final LiteralValueNode literalValueNode;

    public FunctionPatternWithoutArgs(LiteralValueNode literalValueNode) {
      this.literalValueNode = literalValueNode;
    }

    @Override
    public FunctionLikeNode getFunctionNode(String functionName, int cardinality, String moduleFQNString, SourceSection sourceSection) {
      return new LiteralFunctionNode(language, sourceSection, moduleFQNString, functionName, context.globalFrameDescriptor, literalValueNode);
    }
  }

  @Override
  public ModuleNode visitModule(YonaParser.ModuleContext ctx) {
    FQNNode moduleFQN = visitFqn(ctx.fqn());
    String moduleFQNString = ctx.fqn().getText();
    moduleStack.push(moduleFQN);
    NonEmptyStringListNode exports = visitNonEmptyListOfNames(ctx.nonEmptyListOfNames());

    int functionPatternsCount = ctx.function().size();
    Map<String, FunctionPattern> functionPatterns = new HashMap<>();
    Map<String, Integer> functionCardinality = new HashMap<>();
    Map<String, SourceSection> functionSourceSections = new HashMap<>();
    Dict records = Dict.EMPTY;

    String lastFunctionName = null;

    for (int i = 0; i < ctx.record().size(); i++) {
      YonaParser.RecordContext recordContext = ctx.record(i);
      String[] fields = new String[recordContext.identifier().size()];

      for (int j = 0; j < recordContext.identifier().size(); j++) {
        fields[j] = recordContext.identifier(j).getText();
      }

      records = records.add(recordContext.UPPERCASE_NAME().getText(), fields);
    }

    for (int i = 0; i < functionPatternsCount; i++) {
      YonaParser.FunctionContext functionContext = ctx.function(i);

      String functionName = functionContext.name().getText();
      Token functionNameStartToken = functionContext.name().start;
      Token functionBodyStopToken = functionContext.functionBody().stop;

      if (lastFunctionName != null && !lastFunctionName.equals(functionName) && functionPatterns.containsKey(functionName)) {
        throw new ParseError(source, functionNameStartToken.getLine(), functionNameStartToken.getCharPositionInLine() + 1, functionName.length(), "Function %s was already defined previously.".formatted(functionName));
      }
      lastFunctionName = functionName;

      if (!functionSourceSections.containsKey(functionName)) {
        functionSourceSections.put(functionName, source.createSection(functionNameStartToken.getLine(), functionNameStartToken.getCharPositionInLine() + 1, functionBodyStopToken.getLine(), functionBodyStopToken.getCharPositionInLine() + 1));
      } else {
        SourceSection sourceSection = functionSourceSections.get(functionName);
        functionSourceSections.put(functionName, source.createSection(sourceSection.getStartLine(), sourceSection.getStartColumn(), functionBodyStopToken.getLine(), functionBodyStopToken.getCharPositionInLine() + 1));
      }

      int patternsLength = functionContext.pattern().size();

      if (!functionCardinality.containsKey(functionName)) {
        functionCardinality.put(functionName, patternsLength);
      } else if (!functionCardinality.get(functionName).equals(patternsLength)) {
        throw new ParseError(source, functionNameStartToken.getLine(), functionNameStartToken.getCharPositionInLine() + 1, functionName.length(), "Function %s is defined using patterns of varying size.".formatted(functionName));
      }

      if (patternsLength == 0) {
        ExpressionNode expressionNode = functionContext.functionBody().bodyWithoutGuard().expression().accept(this);
        if (expressionNode instanceof LiteralValueNode literalValueNode) {
          functionPatterns.put(functionName, new FunctionPatternWithoutArgs(literalValueNode));
        } else {
          FunctionPatternWithArgs functionPattern = new FunctionPatternWithArgs();
          functionPattern.patternNodes.add(new PatternNode(new UnderscoreMatchNode(), expressionNode));
          functionPatterns.put(functionName, functionPattern);
        }
      } else {
        MatchNode[] patterns = new MatchNode[patternsLength];
        for (int j = 0; j < patternsLength; j++) {
          patterns[j] = visitPattern(functionContext.pattern(j));
        }
        MatchNode argPatterns = new TupleMatchNode(patterns);

        FunctionPatternWithArgs functionPattern;
        if (!functionPatterns.containsKey(functionName)) {
          functionPattern = new FunctionPatternWithArgs();
          functionPatterns.put(functionName, functionPattern);
        } else {
          functionPattern = (FunctionPatternWithArgs) functionPatterns.get(functionName);
        }

        YonaParser.FunctionBodyContext functionBodyContext = functionContext.functionBody();
        if (functionBodyContext.bodyWithoutGuard() != null) {
          functionPattern.patternNodes.add(new PatternNode(argPatterns, functionBodyContext.bodyWithoutGuard().expression().accept(this)));
        } else {
          for (int j = 0; j < functionBodyContext.bodyWithGuards().size(); j++) {
            YonaParser.BodyWithGuardsContext bodyWithGuardsContext = functionBodyContext.bodyWithGuards(j);

            ExpressionNode guardExpression = bodyWithGuardsContext.guard.accept(this);
            ExpressionNode expression = bodyWithGuardsContext.expr.accept(this);

            GuardedPattern guardedPattern = withSourceSection(bodyWithGuardsContext, new GuardedPattern(argPatterns, guardExpression, expression));

            functionPattern.patternNodes.add(guardedPattern);
          }
        }
      }
    }

    FunctionLikeNode[] functions = new FunctionLikeNode[functionPatterns.size()];
    int i = 0;
    for (Map.Entry<String, FunctionPattern> pair : functionPatterns.entrySet()) {
      String functionName = pair.getKey();
      FunctionPattern functionPattern = pair.getValue();
      int cardinality = functionCardinality.get(functionName);

      functions[i++] = functionPattern.getFunctionNode(functionName, cardinality, moduleFQNString, functionSourceSections.get(functionName));
    }

    for (String exportedFunction : exports.strings) {
      if (!functionCardinality.containsKey(exportedFunction)) {
        throw new ParseError(source,
          ctx.KW_MODULE().getSymbol().getLine(),
          ctx.KW_MODULE().getSymbol().getCharPositionInLine() + 1,
          ctx.stop.getStopIndex() - ctx.start.getStartIndex(), "Module %s is trying to export function %s that is not defined.".formatted(moduleFQN, exportedFunction));
      }
    }

    moduleStack.pop();
    return withSourceSection(ctx, new ModuleNode(moduleFQN, exports, functions, records));
  }

  @Override
  public NonEmptyStringListNode visitNonEmptyListOfNames(YonaParser.NonEmptyListOfNamesContext ctx) {
    Set<String> names = new HashSet<>();
    for (YonaParser.NameContext text : ctx.name()) {
      names.add(text.getText());
    }
    return withSourceSection(ctx, new NonEmptyStringListNode(names.toArray(new String[]{})));
  }

  @Override
  public FQNNode visitFqn(YonaParser.FqnContext ctx) {
    int packagePartsCount = ctx.packageName() != null ? ctx.packageName().LOWERCASE_NAME().size() : 0;
    String[] packageParts = new String[packagePartsCount];
    for (int i = 0; i < packagePartsCount; i++) {
      packageParts[i] = ctx.packageName().LOWERCASE_NAME(i).getText();
    }
    return withSourceSection(ctx, new FQNNode(packageParts, ctx.moduleName().getText()));
  }

  @Override
  public SymbolNode visitSymbol(YonaParser.SymbolContext ctx) {
    return withSourceSection(ctx, new SymbolNode(ctx.SYMBOL().getText().substring(1)));
  }

  @Override
  public IdentifierNode visitIdentifier(YonaParser.IdentifierContext ctx) {
    String name = ctx.name().getText();
    return withSourceSection(ctx, new IdentifierNode(language, name, moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public CaseNode visitCaseExpr(YonaParser.CaseExprContext ctx) {
    ExpressionNode expr = ctx.expression().accept(this);
    List<PatternMatchable> patternNodes = new ArrayList<>();

    for (int i = 0; i < ctx.patternExpression().size(); i++) {
      YonaParser.PatternExpressionContext patternExpressionContext = ctx.patternExpression(i);

      MatchNode matchExpression = visitPattern(patternExpressionContext.pattern());

      if (patternExpressionContext.patternExpressionWithoutGuard() != null) {
        patternNodes.add(withSourceSection(patternExpressionContext.patternExpressionWithoutGuard(), new PatternNode(matchExpression, patternExpressionContext.patternExpressionWithoutGuard().expression().accept(this))));
      } else {
        for (int j = 0; j < patternExpressionContext.patternExpressionWithGuard().size(); j++) {
          YonaParser.PatternExpressionWithGuardContext withGuardContext = patternExpressionContext.patternExpressionWithGuard(j);

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
  public MatchNode visitPattern(YonaParser.PatternContext ctx) {
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
  public MatchNode visitDataStructurePattern(YonaParser.DataStructurePatternContext ctx) {
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
  public MatchNode visitAsDataStructurePattern(YonaParser.AsDataStructurePatternContext ctx) {
    return withSourceSection(ctx, new AsDataStructureMatchNode(visitIdentifier(ctx.identifier()), visitDataStructurePattern(ctx.dataStructurePattern())));
  }

  @Override
  public MatchNode visitPatternWithoutSequence(YonaParser.PatternWithoutSequenceContext ctx) {
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
  public TupleMatchNode visitTuplePattern(YonaParser.TuplePatternContext ctx) {
    ExpressionNode[] expressions = new ExpressionNode[ctx.pattern().size()];

    for (int i = 0; i < ctx.pattern().size(); i++) {
      expressions[i] = ctx.pattern(i).accept(this);
    }

    return withSourceSection(ctx, new TupleMatchNode(expressions));
  }

  @Override
  public MatchNode visitSequencePattern(YonaParser.SequencePatternContext ctx) {
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
      return withSourceSection(ctx, new SequenceMatchNode(matchNodes));
    }
  }

  @Override
  public HeadTailsMatchNode visitHeadTails(YonaParser.HeadTailsContext ctx) {
    MatchNode[] headPatterns = new MatchNode[ctx.patternWithoutSequence().size()];

    for (int i = 0; i < ctx.patternWithoutSequence().size(); i++) {
      headPatterns[i] = visitPatternWithoutSequence(ctx.patternWithoutSequence(i));
    }

    ExpressionNode tails = ctx.tails().accept(this);

    return withSourceSection(ctx, new HeadTailsMatchNode(headPatterns, tails));
  }

  @Override
  public TailsHeadMatchNode visitTailsHead(YonaParser.TailsHeadContext ctx) {
    MatchNode[] headPatterns = new MatchNode[ctx.patternWithoutSequence().size()];

    for (int i = 0; i < ctx.patternWithoutSequence().size(); i++) {
      headPatterns[i] = visitPatternWithoutSequence(ctx.patternWithoutSequence(i));
    }

    ExpressionNode tails = ctx.tails().accept(this);

    return withSourceSection(ctx, new TailsHeadMatchNode(tails, headPatterns));
  }

  @Override
  public HeadTailsHeadMatchNode visitHeadTailsHead(YonaParser.HeadTailsHeadContext ctx) {
    MatchNode[] leftPatterns = new MatchNode[ctx.leftPattern().size()];
    MatchNode[] rightPatterns = new MatchNode[ctx.rightPattern().size()];

    ExpressionNode tails = ctx.tails().accept(this);

    for (int i = 0; i < ctx.leftPattern().size(); i++) {
      leftPatterns[i] = visitPatternWithoutSequence(ctx.leftPattern(i).patternWithoutSequence());
    }

    for (int i = 0; i < ctx.rightPattern().size(); i++) {
      rightPatterns[i] = visitPatternWithoutSequence(ctx.rightPattern(i).patternWithoutSequence());
    }

    return withSourceSection(ctx, new HeadTailsHeadMatchNode(leftPatterns, tails, rightPatterns));
  }

  @Override
  public DictMatchNode visitDictPattern(YonaParser.DictPatternContext ctx) {
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
  public MatchNode visitRecordPattern(YonaParser.RecordPatternContext ctx) {
    if (ctx.pattern().size() > 0) {
      RecordFieldsMatchNode.RecordPatternFieldNode[] fields = new RecordFieldsMatchNode.RecordPatternFieldNode[ctx.name().size()];

      for (int i = 0; i < ctx.name().size(); i++) {
        fields[i] = new RecordFieldsMatchNode.RecordPatternFieldNode(ctx.name(i).LOWERCASE_NAME().getText(), visitPattern(ctx.pattern(i)));
      }

      return withSourceSection(ctx, new RecordFieldsMatchNode(ctx.recordType().UPPERCASE_NAME().getText(), fields, moduleStack.toArray(new ExpressionNode[]{})));
    } else {
      return withSourceSection(ctx, new RecordTypeMatchNode(ctx.recordType().UPPERCASE_NAME().getText(), moduleStack.toArray(new ExpressionNode[]{})));
    }
  }

  @Override
  public LetNode visitImportExpr(YonaParser.ImportExprContext ctx) {
    List<NameAliasNode> nameAliasNodes = new ArrayList<>();

    for (int i = 0; i < ctx.importClause().size(); i++) {
      YonaParser.ImportClauseContext importClauseContext = ctx.importClause(i);

      if (importClauseContext.moduleImport() != null) {
        nameAliasNodes.add(visitModuleImport(importClauseContext.moduleImport()));
      } else {
        FQNNode fqnNode = visitFqn(importClauseContext.functionsImport().fqn());
        for (YonaParser.FunctionAliasContext nameContext : importClauseContext.functionsImport().functionAlias()) {
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
  public NameAliasNode visitModuleImport(YonaParser.ModuleImportContext ctx) {
    FQNNode fqnNode = visitFqn(ctx.fqn());
    if (ctx.name() == null) {
      return withSourceSection(ctx, new NameAliasNode(fqnNode.moduleName, fqnNode));
    } else {
      return withSourceSection(ctx, new NameAliasNode(ctx.name().getText(), fqnNode));
    }
  }

  @Override
  public ExpressionNode visitDoExpr(YonaParser.DoExprContext ctx) {
    ExpressionNode[] steps = new ExpressionNode[ctx.doOneStep().size()];

    for (int i = 0; i < ctx.doOneStep().size(); i++) {
      steps[i] = visitDoOneStep(ctx.doOneStep(i));
    }

    return withSourceSection(ctx, new DoNode(steps));
  }

  @Override
  public ExpressionNode visitDoOneStep(YonaParser.DoOneStepContext ctx) {
    if (ctx.alias() != null) {
      return visitAlias(ctx.alias());
    } else {
      return ctx.expression().accept(this);
    }
  }

  @Override
  public ExpressionNode visitTryCatchExpr(YonaParser.TryCatchExprContext ctx) {
    ExpressionNode tryExpression = ctx.expression().accept(this);
    List<PatternMatchable> patternNodes = new ArrayList<>();

    for (int i = 0; i < ctx.catchExpr().catchPatternExpression().size(); i++) {
      YonaParser.CatchPatternExpressionContext patternExpressionContext = ctx.catchExpr().catchPatternExpression(i);

      MatchNode matchExpression;

      if (patternExpressionContext.tripplePattern() != null) {
        matchExpression = visitTripplePattern(patternExpressionContext.tripplePattern());
      } else {
        matchExpression = visitUnderscore(patternExpressionContext.underscore());
      }

      if (patternExpressionContext.catchPatternExpressionWithoutGuard() != null) {
        patternNodes.add(withSourceSection(patternExpressionContext.catchPatternExpressionWithoutGuard(), new PatternNode(matchExpression, patternExpressionContext.catchPatternExpressionWithoutGuard().expression().accept(this))));
      } else {
        for (int j = 0; j < patternExpressionContext.catchPatternExpressionWithGuard().size(); j++) {
          YonaParser.CatchPatternExpressionWithGuardContext withGuardContext = patternExpressionContext.catchPatternExpressionWithGuard(j);

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
  public TupleMatchNode visitTripplePattern(YonaParser.TripplePatternContext ctx) {
    ExpressionNode[] expressions = new ExpressionNode[ctx.pattern().size()];

    for (int i = 0; i < ctx.pattern().size(); i++) {
      expressions[i] = ctx.pattern(i).accept(this);
    }

    return withSourceSection(ctx, new TupleMatchNode(expressions));
  }

  @Override
  public ThrowYonaExceptionNode visitRaiseExpr(YonaParser.RaiseExprContext ctx) {
    return withSourceSection(ctx, new ThrowYonaExceptionNode(visitSymbol(ctx.symbol()), visitStringLiteral(ctx.stringLiteral())));
  }

  @Override
  public ExpressionNode visitBacktickExpression(YonaParser.BacktickExpressionContext ctx) {
    ExpressionNode[] argNodes = new ExpressionNode[2];
    argNodes[0] = ctx.left.accept(this);
    argNodes[1] = ctx.right.accept(this);

    YonaParser.CallContext callCtx = ctx.call();
    return createCallNode(ctx, argNodes, callCtx);
  }

  private ExpressionNode createCallNode(ParserRuleContext ctx, ExpressionNode[] argNodes, YonaParser.CallContext callCtx) {
    ExpressionNode[] moduleStackArray = moduleStack.toArray(new ExpressionNode[]{});
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
  public ExpressionNode visitPipeLeftExpression(YonaParser.PipeLeftExpressionContext ctx) {
    return withSourceSection(ctx, new InvokeNode(language, ctx.left.accept(this), new ExpressionNode[]{ctx.right.accept(this)}, moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public ExpressionNode visitPipeRightExpression(YonaParser.PipeRightExpressionContext ctx) {
    return withSourceSection(ctx, new InvokeNode(language, ctx.right.accept(this), new ExpressionNode[]{ctx.left.accept(this)}, moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public SetNode visitSet(YonaParser.SetContext ctx) {
    ExpressionNode[] expressionNodes = new ExpressionNode[ctx.expression().size()];
    for (int i = 0; i < ctx.expression().size(); i++) {
      expressionNodes[i] = ctx.expression(i).accept(this);
    }
    return withSourceSection(ctx, new SetNode(expressionNodes));
  }

  @Override
  public ExpressionNode visitSequenceGeneratorExpr(YonaParser.SequenceGeneratorExprContext ctx) {
    ExpressionNode reducer = ctx.reducer.accept(this);
    ExpressionNode condition = visitOptional(ctx.condition);
    MatchNode[] stepMatchNodes;
    ExpressionNode stepExpression = ctx.stepExpression.accept(this);

    if (ctx.collectionExtractor().valueCollectionExtractor() != null) {
      stepMatchNodes = new MatchNode[]{visitIdentifierOrUnderscore(ctx.collectionExtractor().valueCollectionExtractor().identifierOrUnderscore())};
    } else {
      stepMatchNodes = new MatchNode[]{
        visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().key),
        visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().val)
      };
    }

    return withSourceSection(ctx, new GeneratorNode(language, GeneratedCollection.SEQ, reducer, condition, stepMatchNodes, stepExpression, moduleStack.toArray(new ExpressionNode[]{}), currentModuleName()));
  }

  @Override
  public ExpressionNode visitSetGeneratorExpr(YonaParser.SetGeneratorExprContext ctx) {
    ExpressionNode reducer = ctx.reducer.accept(this);
    ExpressionNode condition = visitOptional(ctx.condition);
    MatchNode[] stepMatchNodes;
    ExpressionNode stepExpression = ctx.stepExpression.accept(this);

    if (ctx.collectionExtractor().valueCollectionExtractor() != null) {
      stepMatchNodes = new MatchNode[]{visitIdentifierOrUnderscore(ctx.collectionExtractor().valueCollectionExtractor().identifierOrUnderscore())};
    } else {
      stepMatchNodes = new MatchNode[]{
        visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().key),
        visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().val)
      };
    }

    return withSourceSection(ctx, new GeneratorNode(language, GeneratedCollection.SET, reducer, condition, stepMatchNodes, stepExpression, moduleStack.toArray(new ExpressionNode[]{}), currentModuleName()));
  }

  @Override
  public ExpressionNode visitDictGeneratorExpr(YonaParser.DictGeneratorExprContext ctx) {
    TupleNode reducer = visitDictGeneratorReducer(ctx.dictGeneratorReducer());
    ExpressionNode condition = visitOptional(ctx.condition);
    MatchNode[] stepMatchNodes;
    ExpressionNode stepExpression = ctx.stepExpression.accept(this);

    if (ctx.collectionExtractor().valueCollectionExtractor() != null) {
      stepMatchNodes = new MatchNode[]{visitIdentifierOrUnderscore(ctx.collectionExtractor().valueCollectionExtractor().identifierOrUnderscore())};
    } else {
      stepMatchNodes = new MatchNode[]{
        visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().key),
        visitIdentifierOrUnderscore(ctx.collectionExtractor().keyValueCollectionExtractor().val)
      };
    }

    return withSourceSection(ctx, new GeneratorNode(language, GeneratedCollection.DICT, reducer, condition, stepMatchNodes, stepExpression, moduleStack.toArray(new ExpressionNode[]{}), currentModuleName()));
  }

  @Override
  public MatchNode visitIdentifierOrUnderscore(YonaParser.IdentifierOrUnderscoreContext ctx) {
    if (ctx.identifier() != null) {
      return new ValueMatchNode(visitIdentifier(ctx.identifier()));
    } else {
      return visitUnderscore(ctx.underscore());
    }
  }

  @Override
  public TupleNode visitDictGeneratorReducer(YonaParser.DictGeneratorReducerContext ctx) {
    return withSourceSection(ctx, new TupleNode(ctx.dictKey().accept(this), ctx.dictVal().accept(this)));
  }

  @Override
  public FieldAccessNode visitFieldAccessExpr(YonaParser.FieldAccessExprContext ctx) {
    return withSourceSection(ctx, new FieldAccessNode(visitIdentifier(ctx.identifier()), ctx.name().getText(), moduleStack.toArray(new ExpressionNode[]{})));
  }

  @Override
  public RecordUpdateNode visitFieldUpdateExpr(YonaParser.FieldUpdateExprContext ctx) {
    RecordFieldValueNode[] fields = new RecordFieldValueNode[ctx.name().size()];

    for (int i = 0; i < ctx.name().size(); i++) {
      fields[i] = new RecordFieldValueNode(ctx.name(i).LOWERCASE_NAME().getText(), ctx.expression(i).accept(this));
    }

    return new RecordUpdateNode(visitIdentifier(ctx.identifier()), fields, moduleStack.toArray(new ExpressionNode[]{}));
  }

  @Override
  public WithExpression visitWithExpression(YonaParser.WithExpressionContext ctx) {
    String name = null;
    if (ctx.withExpr().name() != null) {
      name = ctx.withExpr().name().getText();
    }
    YonaParser.ExpressionContext bodyCtx = ctx.withExpr().body;
    FunctionNode bodyNode = withSourceSection(bodyCtx, new FunctionNode(language, sourceSectionForRule(bodyCtx), currentModuleName(), nextLambdaName(), 0, context.globalFrameDescriptor, bodyCtx.accept(this)));
    return withSourceSection(ctx, new WithExpression(language, name, ctx.withExpr().context.accept(this), bodyNode, ctx.withExpr().KW_DAEMON() != null));
  }

  public ExpressionNode visitOptional(ParserRuleContext ctx) {
    if (ctx == null) return null;
    else return ctx.accept(this);
  }

  private <T extends ExpressionNode> T withSourceSection(ParserRuleContext parserRuleContext, T expressionNode) {
    final SourceSection sourceSection = sourceSectionForRule(parserRuleContext);
    expressionNode.setSourceSection(sourceSection);
    return expressionNode;
  }

  private SourceSection sourceSectionForRule(ParserRuleContext parserRuleContext) {
    final SourceSection sourceSection;

    sourceSection = source.createSection(
      parserRuleContext.start.getLine(),
      1,
      parserRuleContext.stop.getLine(),
      source.getLineLength(parserRuleContext.stop.getLine())
    );
    return sourceSection;
  }
}
