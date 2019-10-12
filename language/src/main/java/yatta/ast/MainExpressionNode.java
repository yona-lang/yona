package yatta.ast;

import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.call.BuiltinCallNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.expression.value.FQNNode;
import yatta.ast.expression.value.ModuleFunctionNode;
import yatta.runtime.*;
import yatta.runtime.async.Promise;

import java.util.ArrayList;
import java.util.List;

@NodeInfo(shortName = "main")
public final class MainExpressionNode extends ExpressionNode {
  @Child
  public ExpressionNode expressionNode;
  @Children
  private FQNNode[] moduleStack;
  private final YattaLanguage language;

  public MainExpressionNode(YattaLanguage language, ExpressionNode expressionNode, FQNNode[] moduleStack) {
    this.language = language;
    this.expressionNode = expressionNode;
    this.moduleStack = moduleStack;
  }

  @Override
  public String toString() {
    return "MainExpressionNode{" +
        "expressionNode=" + expressionNode +
        '}';
  }

  private void writeBuiltinsOnStack(VirtualFrame frame, Builtins builtins, Context context) {
    builtins.builtins.forEach((name, nodeFactory) -> {
      int cardinality = nodeFactory.getExecutionSignature().size();
      ModuleFunctionNode functionNode = new ModuleFunctionNode(language, Context.BUILTIN_SOURCE.createUnavailableSection(), name, cardinality, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), new BuiltinCallNode(nodeFactory));

      InvokeNode invokeNode = new InvokeNode(language, functionNode, new ExpressionNode[]{}, moduleStack);
      context.insertGlobal(name, invokeNode.executeGeneric(frame));
    });
}

  public void writeModuleOnStack(VirtualFrame frame, String fqn, Builtins builtins, Context context) {
    final List<String> exports = new ArrayList<>(builtins.builtins.size());
    final List<Function> functions = new ArrayList<>(builtins.builtins.size());

    builtins.builtins.forEach((name, nodeFactory) -> {
      int argumentsCount = nodeFactory.getExecutionSignature().size();
      ModuleFunctionNode functionNode = new ModuleFunctionNode(language, Context.BUILTIN_SOURCE.createUnavailableSection(), name, argumentsCount, new FrameDescriptor(UninitializedFrameSlot.INSTANCE), new BuiltinCallNode(nodeFactory));

      exports.add(name);
      try {
        functions.add(functionNode.executeFunction(frame));
      } catch (UnexpectedResultException e) {
      }
    });

    Module module = new Module(fqn, exports, functions);
    context.insertGlobal(fqn, module);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      TruffleLanguage.ContextReference<Context> contextRef = lookupContextReference(YattaLanguage.class);
      Context context = contextRef.get();

      writeBuiltinsOnStack(frame, context.builtins, context);
      context.builtinModules.builtins.forEach((fqn, builtins) -> {
        writeModuleOnStack(frame, fqn, builtins, context);
      });

      Object result = expressionNode.executeGeneric(frame);
      if (result instanceof Promise) {
        Promise promise = (Promise) result;
        try {
          result = Promise.await(promise);
        } catch (YattaException e) {
          throw e;
        } catch (Throwable e) {
          throw new YattaException(e, this);
        }
      }

      return executeIfFunction(result, frame);
    } finally {
      Context.getCurrent().threading.dispose();
    }
  }

  private Object executeIfFunction(Object result, VirtualFrame frame) {
    if (result instanceof Function) {
      Function function = (Function) result;
      if (function.getCardinality() == 0) {
        return function.getCallTarget().getRootNode().execute(frame);
      }
    }

    return result;
  }
}
