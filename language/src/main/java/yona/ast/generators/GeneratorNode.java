package yona.ast.generators;

import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.ast.ExpressionNode;
import yona.ast.call.InvokeNode;
import yona.ast.expression.CaseNode;
import yona.ast.expression.value.FunctionNode;
import yona.ast.local.ReadArgumentNode;
import yona.ast.pattern.MatchNode;
import yona.ast.pattern.PatternNode;
import yona.ast.pattern.TupleMatchNode;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.UninitializedFrameSlot;

@NodeInfo(shortName = "generator")
public final class GeneratorNode extends ExpressionNode {
  @Child
  private InvokeNode callNode;
  private final String moduleFQN;

  public GeneratorNode(YonaLanguage language, GeneratedCollection type, ExpressionNode reducer, ExpressionNode condition, MatchNode[] stepNames, ExpressionNode stepExpression, ExpressionNode[] moduleStack, String moduleFQN) {
    this.callNode = getGeneratorNode(language, type, reducer, condition, stepNames, stepExpression, moduleStack);
    this.moduleFQN = moduleFQN;
  }

  protected InvokeNode getGeneratorNode(YonaLanguage language, GeneratedCollection type, ExpressionNode reducer, ExpressionNode condition, MatchNode[] stepMatchNodes, ExpressionNode stepExpression, ExpressionNode[] moduleStack) {
    Context context = Context.getCurrent();

    Function mapTransducer = context.lookupGlobalFunction("Transducers", "map");
    Function finalShapeReducer = context.lookupGlobalFunction("Reducers", reducerForGeneratedCollection(type));
    InvokeNode toSeqInvoke = new InvokeNode(language, finalShapeReducer, new ExpressionNode[]{}, moduleStack);

    MatchNode argPatterns;
    if (stepMatchNodes.length == 1) {
      /* If there is only one stepName (aka bound variable), then this is a seq/set element */
      argPatterns = stepMatchNodes[0];
    } else {
      /* Otherwise, then this is a dict key/value tuple, so the appropriate pattern for a tuple must be constructed */
      argPatterns = new TupleMatchNode(stepMatchNodes);
    }

    ExpressionNode reducerBodyNode = new CaseNode(new ReadArgumentNode(0), new PatternNode[]{new PatternNode(argPatterns, reducer)});
    reducerBodyNode.addRootTag();

    FunctionNode reduceFunction = new FunctionNode(language, reducer.getSourceSection(), this.moduleFQN, "$" + type.toLowerString() + "_reducer", 1, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), reducerBodyNode);
    InvokeNode mapInvoke = new InvokeNode(language, mapTransducer, new ExpressionNode[]{reduceFunction, toSeqInvoke}, moduleStack);

    InvokeNode filterInvoke = null;
    if (condition != null) {
      Function filterTransducer = context.lookupGlobalFunction("Transducers", "filter");
      ExpressionNode conditionBodyNode = new CaseNode(new ReadArgumentNode(0), new PatternNode[]{new PatternNode(argPatterns, condition)});
      conditionBodyNode.addRootTag();
      FunctionNode conditionFunction = new FunctionNode(language, reducer.getSourceSection(), this.moduleFQN, "$" + type.toLowerString() + "_filter", 1, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), conditionBodyNode);
      filterInvoke = new InvokeNode(language, filterTransducer, new ExpressionNode[]{conditionFunction, mapInvoke}, moduleStack);
    }

    ExpressionNode[] reduceArgs = new ExpressionNode[]{filterInvoke == null ? mapInvoke : filterInvoke, stepExpression};
    return new InvokeNode(language, context.lookupGlobalFunction("Reducers", "reduce"), reduceArgs, moduleStack);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    this.replace(callNode);
    return callNode.executeGeneric(frame);
  }

  @Override
  protected String[] requiredIdentifiers() {
    return callNode.getRequiredIdentifiers();
  }

  private String reducerForGeneratedCollection(GeneratedCollection type) {
    return switch (type) {
      case SEQ -> "to_seq";
      case SET -> "to_set";
      case DICT -> "to_dict";
    };
  }
}
