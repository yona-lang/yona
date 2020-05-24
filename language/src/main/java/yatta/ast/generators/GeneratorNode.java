package yatta.ast.generators;

import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.expression.CaseNode;
import yatta.ast.expression.value.FunctionNode;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.pattern.MatchNode;
import yatta.ast.pattern.PatternNode;
import yatta.ast.pattern.TupleMatchNode;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.UninitializedFrameSlot;

@NodeInfo(shortName = "generator")
public final class GeneratorNode extends ExpressionNode {
  @Child
  private InvokeNode callNode;
  private final String moduleFQN;

  public GeneratorNode(YattaLanguage language, GeneratedCollection type, ExpressionNode reducer, ExpressionNode condition, MatchNode[] stepNames, ExpressionNode stepExpression, ExpressionNode[] moduleStack, String moduleFQN) {
    this.callNode = getGeneratorNode(language, type, reducer, condition, stepNames, stepExpression, moduleStack);
    this.moduleFQN = moduleFQN;
  }

  protected InvokeNode getGeneratorNode(YattaLanguage language, GeneratedCollection type, ExpressionNode reducer, ExpressionNode condition, MatchNode[] stepMatchNodes, ExpressionNode stepExpression, ExpressionNode[] moduleStack) {
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

    ExpressionNode[] reduceArgs = new ExpressionNode[]{stepExpression, filterInvoke == null ? mapInvoke : filterInvoke};
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
    switch (type) {
      case SEQ:
        return "to_seq";
      case SET:
        return "to_set";
      case DICT:
        return "to_dict";
    }
    return "what";  // wtf is this required
  }
}
