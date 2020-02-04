package yatta.ast.generators;

import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.builtin.BuiltinNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.expression.CaseNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.FunctionNode;
import yatta.ast.expression.value.TupleNode;
import yatta.ast.local.ReadArgumentNode;
import yatta.ast.pattern.MatchNode;
import yatta.ast.pattern.PatternNode;
import yatta.ast.pattern.TupleMatchNode;
import yatta.ast.pattern.ValueMatchNode;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.UninitializedFrameSlot;

@NodeInfo
public final class SequenceGeneratorNode extends ExpressionNode {
  private BuiltinNode callNode;

  public SequenceGeneratorNode(YattaLanguage language, ExpressionNode reducer, ExpressionNode condition, String stepName, ExpressionNode stepExpression, ExpressionNode[] moduleStack) {
    Context context = Context.getCurrent();

    Function mapTransducer = context.lookupGlobalFunction("Transducers", "map");
    Function toSeqTransducer = context.lookupGlobalFunction("Reducers", "to_seq");
    InvokeNode toSeqInvoke = new InvokeNode(language, toSeqTransducer, new ExpressionNode[] {}, moduleStack);

    MatchNode[] patterns = new MatchNode[] {new ValueMatchNode(new IdentifierNode(language, stepName, moduleStack))};
    TupleMatchNode argPatterns = new TupleMatchNode(patterns);

    ExpressionNode[] argumentNodes = new ExpressionNode[] {new ReadArgumentNode(0)};

    TupleNode argsTuple = new TupleNode(argumentNodes);
    ExpressionNode reducerBodyNode = new CaseNode(argsTuple, new PatternNode[]{new PatternNode(argPatterns, reducer)});
    reducerBodyNode.addRootTag();
    FunctionNode reduceFunction = new FunctionNode(language, reducer.getSourceSection(), "$seq_reducer", 1, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), reducerBodyNode);
    InvokeNode mapInvoke = new InvokeNode(language, mapTransducer, new ExpressionNode[]{reduceFunction, toSeqInvoke}, moduleStack);

    InvokeNode filterInvoke = null;
    if (condition != null) {
      Function filterTransducer = context.lookupGlobalFunction("Transducers", "filter");
      ExpressionNode conditionBodyNode = new CaseNode(argsTuple, new PatternNode[]{new PatternNode(argPatterns, condition)});
      conditionBodyNode.addRootTag();
      FunctionNode conditionFunction = new FunctionNode(language, reducer.getSourceSection(), "$seq_filter", 1, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), conditionBodyNode);
      filterInvoke = new InvokeNode(language, filterTransducer, new ExpressionNode[]{conditionFunction, mapInvoke}, moduleStack);
    }

    ExpressionNode[] reduceArgs = new ExpressionNode[] {stepExpression, filterInvoke == null ? mapInvoke : filterInvoke};
    this.callNode = context.builtinModules.lookup("Seq", "reducel").node.createNode((Object) reduceArgs);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    this.replace(callNode);
    return callNode.executeGeneric(frame);
  }
}
