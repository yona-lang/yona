package yatta.ast.call;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.JavaMethodRootNode;
import yatta.runtime.Function;
import yatta.runtime.NativeObject;
import yatta.runtime.YattaModule;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.PolyglotException;

import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class ModuleCallNode extends ExpressionNode {
  @Child
  private ExpressionNode nameNode;
  @Children
  private ExpressionNode[] argumentNodes;
  private String functionName;
  @Children
  private ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode
  private final YattaLanguage language;

  public ModuleCallNode(YattaLanguage language, ExpressionNode nameNode, String functionName, ExpressionNode[] argumentNodes, ExpressionNode[] moduleStack) {
    this.language = language;
    this.nameNode = nameNode;
    this.functionName = functionName;
    this.argumentNodes = argumentNodes;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ModuleCallNode that = (ModuleCallNode) o;
    return Objects.equals(nameNode, that.nameNode) &&
        Arrays.equals(argumentNodes, that.argumentNodes);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(nameNode);
    result = 31 * result + Arrays.hashCode(argumentNodes);
    return result;
  }

  @Override
  public String toString() {
    return "ModuleCallNode{" +
        "nameNode=" + nameNode +
        ", functionName=" + functionName +
        ", argumentNodes=" + Arrays.toString(argumentNodes) +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object executedName = nameNode.executeGeneric(frame);

    if (executedName instanceof Promise) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      MaterializedFrame materializedFrame = frame.materialize();

      return ((Promise) executedName).map(maybeModule -> invokeModuleFunction(materializedFrame, maybeModule), this);
    } else if (executedName instanceof YattaModule || executedName instanceof NativeObject) {
      return invokeModuleFunction(frame, executedName);
    } else {
      throw new YattaException("Unexpected error while invoking a module function: : returned value is not a Yatta Module, nor a Native Object", this);
    }
  }

  private Object invokeModuleFunction(VirtualFrame frame, Object maybeModule) {
    if (maybeModule instanceof YattaModule) {
      YattaModule module = (YattaModule) maybeModule;
      if (!module.getExports().contains(functionName)) {
        throw new YattaException("Function " + functionName + " is not present in " + module, this);
      } else {
        Function function = module.getFunctions().get(functionName);
        InvokeNode invokeNode = new InvokeNode(language, function, argumentNodes, moduleStack);
        this.replace(invokeNode);
        return invokeNode.executeGeneric(frame);
      }
    } else if (maybeModule instanceof NativeObject) {
      CompilerDirectives.transferToInterpreterAndInvalidate();

      NativeObject nativeObject = (NativeObject) maybeModule;
      Object obj = nativeObject.getValue();
      Method method = lookupAccessibleMethod(obj, obj.getClass());

      if (method != null) {
        Function javaFunction = JavaMethodRootNode.buildFunction(language, method, frame.getFrameDescriptor(), nativeObject.getValue());
        InvokeNode invokeNode = new InvokeNode(language, javaFunction, argumentNodes, moduleStack);
        this.replace(invokeNode);
        return invokeNode.executeGeneric(frame);
      } else {
        throw new PolyglotException(String.format("Unable to find an accessible method '%s' in object '%s'.", functionName, obj), this);
      }
    } else {
      throw new YattaException("Unexpected error while invoking a module function: : returned value is not a Yatta Module", this);
    }
  }

  @ExplodeLoop
  private Method lookupAccessibleMethod(Object obj, Class<?> cls) {
    Method[] methods = cls.getMethods();
    CompilerAsserts.compilationConstant(methods.length);
    for (int i = 0; i < methods.length; i++) {
      Method method = methods[i];
      if (method.getName().equals(functionName)) {
        if (method.canAccess(obj)) {
          return method;
        } else {
          Class<?> supercls = cls.getSuperclass();
          if (supercls != null) {
            Method possibleMethod = lookupAccessibleMethod(obj, supercls);
            if (possibleMethod != null) {
              return possibleMethod;
            }
          }
          for (Class<?> intf : cls.getInterfaces()) {
            Method possibleMethod = lookupAccessibleMethod(obj, intf);
            if (possibleMethod != null) {
              return possibleMethod;
            }
          }
        }
      }
    }
    return null;
  }
}
