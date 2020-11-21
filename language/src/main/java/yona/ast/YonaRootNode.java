package yona.ast;

import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.TruffleStackTraceElement;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.RootNode;
import yona.runtime.Seq;
import yona.runtime.Tuple;
import yona.runtime.Unit;

public abstract class YonaRootNode extends RootNode {
  protected YonaRootNode(TruffleLanguage<?> language, FrameDescriptor frameDescriptor) {
    super(language, frameDescriptor);
  }

  public static Tuple translateSTE(TruffleStackTraceElement element) {
    Node location = element.getLocation();
    RootNode rootNode = element.getTarget().getRootNode();
    if (location != null && location.getSourceSection() != null) {
      return new Tuple(
          Seq.fromCharSequence(rootNode.getSourceSection().getSource().getName()),
          Seq.fromCharSequence(rootNode.getQualifiedName()),
          location.getSourceSection().getStartLine(),
          location.getSourceSection().getStartColumn()
      );
    } else if (rootNode.getSourceSection() != null) {
      return new Tuple(
          Seq.fromCharSequence(rootNode.getSourceSection().getSource().getName()),
          Seq.fromCharSequence(rootNode.getQualifiedName()),
          Unit.INSTANCE,
          Unit.INSTANCE
      );
    }

    return null;
  }

  @Override
  public Object translateStackTraceElement(TruffleStackTraceElement element) {
    return translateSTE(element);
  }
}
