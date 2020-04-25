package yatta.ast;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.RootNode;
import com.oracle.truffle.api.source.SourceSection;
import yatta.TypesGen;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.local.ReadArgumentNode;
import yatta.runtime.*;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

@NodeInfo(language = "yatta", description = "Java method root call")
public class JavaMethodRootNode extends RootNode {
  private Method method;
  private Object object;
  private String moduleFQN;
  private final SourceSection sourceSection;

  public static Function buildFunction(YattaLanguage language, Method method, FrameDescriptor frameDescriptor, Object object) {
    String fqn = method.getDeclaringClass().getName().replace(".", "\\");
    return new Function(
        fqn,
        method.getName(),
        Truffle.getRuntime().createCallTarget(new JavaMethodRootNode(language, frameDescriptor, method, object, Context.JAVA_SOURCE_SECTION, fqn)),
        method.getParameterCount());
  }

  public JavaMethodRootNode(YattaLanguage language, FrameDescriptor frameDescriptor, Method method,
                            Object object, SourceSection sourceSection, String moduleFQN) {
    super(language, frameDescriptor);
    this.method = method;
    this.object = object;
    this.moduleFQN = moduleFQN;
    this.sourceSection = sourceSection;
  }

  @Override
  public SourceSection getSourceSection() {
    return sourceSection;
  }

  @Override
  public Object execute(VirtualFrame frame) {
    Object[] args = readArgs(frame);
    try {
      Object result = method.invoke(object, args);
      return TypesGen.foreignResultToYattaType(result);
    } catch (IllegalAccessException e) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new YattaException(e, this);
    } catch (InvocationTargetException e) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new YattaException(e.getCause(), this);
    }
  }

  @ExplodeLoop
  private Object[] readArgs(VirtualFrame frame) {
    CompilerAsserts.compilationConstant(method.getParameterCount());
    Object[] args = new Object[method.getParameterCount()];
    for (int i = 0; i < method.getParameterCount(); i++) {
      Object arg = new ReadArgumentNode(i).executeGeneric(frame);
      if (arg instanceof NativeObject) {
        arg = ((NativeObject) arg).getValue();
      } else if (arg instanceof Seq) {
        if (((Seq) arg).isString()) {
          arg = ((Seq) arg).asJavaString(this);
        } else {
          arg = ((Seq) arg).toArray();
        }
      } else if (arg instanceof Tuple) {
        arg = ((Tuple) arg).toArray();
      } else if (arg instanceof Dict) {
        arg = ((Dict) arg).toMap();
      }
      args[i] = arg;
    }
    return args;
  }

  @Override
  public String getName() {
    if (moduleFQN != null) {
      return moduleFQN + "::" + method.getName();
    } else {
      return method.getName();
    }
  }

  @Override
  public String toString() {
    return "java-root " + method.getName();
  }
}
