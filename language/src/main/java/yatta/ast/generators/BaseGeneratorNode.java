package yatta.ast.generators;

import com.oracle.truffle.api.frame.FrameDescriptor;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.expression.CaseNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.FunctionNode;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.pattern.MatchNode;
import yatta.ast.pattern.PatternNode;
import yatta.ast.pattern.TupleMatchNode;
import yatta.ast.pattern.ValueMatchNode;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.UninitializedFrameSlot;

abstract class BaseGeneratorNode extends ExpressionNode {
  private String reducerForGeneratedCollection(GeneratedCollection type) {
    switch (type) {
      case SEQ: return "to_seq";
      case SET: return "to_set";
      case DICT: return "to_dict";
    }
    return "what";  // wtf is this required
  }

  protected InvokeNode getGeneratorNode(YattaLanguage language, GeneratedCollection type, ExpressionNode reducer, ExpressionNode condition, String[] stepNames, ExpressionNode stepExpression, ExpressionNode[] moduleStack) {
    Context context = Context.getCurrent();

    Function mapTransducer = context.lookupGlobalFunction("Transducers", "map");
    Function finalShapeReducer = context.lookupGlobalFunction("Reducers", reducerForGeneratedCollection(type));
    InvokeNode toSeqInvoke = new InvokeNode(language, finalShapeReducer, new ExpressionNode[] {}, moduleStack);

    MatchNode argPatterns;
    if (stepNames.length == 1) {
      /* If there is only one stepName (aka bound variable), then this is a seq/set element */
      argPatterns = new ValueMatchNode(new IdentifierNode(language, stepNames[0], moduleStack));
    } else {
      /* Otherwise, then this is a dict key/value tuple, so the appropriate pattern for a tuple must be constructed */
      MatchNode[] patterns = new MatchNode[stepNames.length];
      for (int i = 0; i < stepNames.length; i++) {
        patterns[i] = new ValueMatchNode(new IdentifierNode(language, stepNames[i], moduleStack));
      }
      argPatterns = new TupleMatchNode(patterns);
    }

    ExpressionNode reducerBodyNode = new CaseNode(new ReadArgumentNode(0), new PatternNode[]{new PatternNode(argPatterns, reducer)});
    reducerBodyNode.addRootTag();

    FunctionNode reduceFunction = new FunctionNode(language, reducer.getSourceSection(), "$" + type.toLowerString() + "_reducer", 1, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), reducerBodyNode);
    InvokeNode mapInvoke = new InvokeNode(language, mapTransducer, new ExpressionNode[]{reduceFunction, toSeqInvoke}, moduleStack);

    InvokeNode filterInvoke = null;
    if (condition != null) {
      Function filterTransducer = context.lookupGlobalFunction("Transducers", "filter");
      ExpressionNode conditionBodyNode = new CaseNode(new ReadArgumentNode(0), new PatternNode[]{new PatternNode(argPatterns, condition)});
      conditionBodyNode.addRootTag();
      FunctionNode conditionFunction = new FunctionNode(language, reducer.getSourceSection(), "$" + type.toLowerString() + "_filter", 1, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), conditionBodyNode);
      filterInvoke = new InvokeNode(language, filterTransducer, new ExpressionNode[]{conditionFunction, mapInvoke}, moduleStack);
    }

    ExpressionNode[] reduceArgs = new ExpressionNode[] {stepExpression, filterInvoke == null ? mapInvoke : filterInvoke};
    return new InvokeNode(language, context.lookupGlobalFunction("Reducers", "reduce"), reduceArgs, moduleStack);
  }
}
