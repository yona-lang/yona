package yona;

import com.oracle.truffle.api.CompilerDirectives.TruffleBoundary;
import com.oracle.truffle.api.TruffleStackTrace;
import com.oracle.truffle.api.TruffleStackTraceElement;
import com.oracle.truffle.api.exception.AbstractTruffleException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.SourceSection;
import yona.ast.YonaRootNode;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Tuple;
import yona.runtime.Unit;

@ExportLibrary(InteropLibrary.class)
public class YonaException extends AbstractTruffleException {
  private static final long serialVersionUID = -6799734410727348507L;

  @TruffleBoundary
  public YonaException(String message, Node location) {
    super(message, location);
  }

  @TruffleBoundary
  public YonaException(String message, Throwable cause, Node location) {
    super(message, cause, UNLIMITED_STACK_TRACE, location);
  }

  @TruffleBoundary
  public YonaException(Seq message, Node location) {
    this(message.asJavaString(location), location);
  }

  @TruffleBoundary
  public YonaException(Seq message, Throwable cause, Node location) {
    this(message.asJavaString(location), cause, location);
  }

  @TruffleBoundary
  public YonaException(Throwable cause, Node location) {
    super(cause.getMessage() != null ? cause.getClass().getName() + ": " + cause.getMessage() : cause.getClass().getName(), cause, UNLIMITED_STACK_TRACE, location);
  }

  @TruffleBoundary
  public Tuple asTuple() {
    return Tuple.allocate(null, Context.get(null).lookupExceptionSymbol(this.getClass()), Seq.fromCharSequence(getMessage()), stacktraceToSequence(this, null));
  }

  @ExportMessage
  public boolean isException() {
    return true;
  }

  @ExportMessage
  public boolean hasExceptionCause() {
    return this.getCause() != null;
  }

  @ExportMessage
  public Object getExceptionCause() {
    return this.getCause();
  }


  @ExportMessage
  public boolean hasExceptionMessage() {
    return this.getMessage() != null;
  }

  @ExportMessage
  public Object getExceptionMessage() {
    return this.getMessage();
  }

  @ExportMessage
  public RuntimeException throwException() {
    return this;
  }

  @ExportMessage
  public Object getExceptionStackTrace() {
//    return Tuple.allocate(Context.getCurrent().lookupExceptionSymbol(this.getClass()), getMessage(), stacktraceToSequence(this).foldLeft(Seq.EMPTY, (acc, el) -> acc.insertFirst(stackFrameTupleToString((Tuple) el))));
    return stacktraceToSequence(this, null).foldLeft(Seq.EMPTY, (acc, el) -> acc.insertFirst(stackFrameTupleToString((Tuple) el)));
  }

  @ExportMessage
  public boolean hasExceptionStackTrace() {
    return true;
  }

  @TruffleBoundary
  public static String prettyPrint(String message, org.graalvm.polyglot.SourceSection sourceLocation) {
    StringBuilder sb = new StringBuilder();
    sb.append(message);
    sb.append(" at ");
    if (sourceLocation != null) {
      sb.append(sourceLocation.getSource().getName());
      sb.append(":\n");
      sb.append(sourceLocation.getCharacters());
    } else {
      sb.append("<unknown>");
    }
    return sb.toString();
  }

  /**
   * Provides a user-readable message for run-time type errors. Yona is strongly typed, i.e., there
   * are no automatic type conversions of values.
   */
  @TruffleBoundary
  public static YonaException typeError(Node operation, Object... values) {
    InteropLibrary interopLibrary = InteropLibrary.getFactory().getUncached();
    StringBuilder result = new StringBuilder();
    result.append("Type error");

    if (operation != null) {
      SourceSection ss = operation.getEncapsulatingSourceSection();
      if (ss != null && ss.isAvailable()) {
        result.append(" at ").append(ss.getSource().getName()).append(" line ").append(ss.getStartLine()).append(" col ").append(ss.getStartColumn());
      }
    }

    result.append(": operation");
    if (operation != null) {
      NodeInfo nodeInfo = Context.lookupNodeInfo(operation.getClass());
      if (nodeInfo != null) {
        result.append(" \"").append(nodeInfo.shortName()).append("\"");
      }
    }

    result.append(" not defined for");

    String sep = " ";
    for (Object value : values) {
      result.append(sep);
      sep = ", ";
      if (value == null || interopLibrary.isNull(value)) {
        result.append(interopLibrary.toDisplayString(value));
      } else {
        if (interopLibrary.isString(value)) {
          result.append("\"");
        }
        result.append(interopLibrary.toDisplayString(value));
        if (interopLibrary.isString(value)) {
          result.append("\"");
        }
      }
    }

    return new YonaException(result.toString(), operation);
  }

  @TruffleBoundary
  public static Seq stacktraceToSequence(Throwable throwable, Node node) {
    Seq stackTraceSequence = Seq.EMPTY;

    for (TruffleStackTraceElement stackTraceElement : TruffleStackTrace.getStackTrace(throwable)) {
      Tuple tuple = YonaRootNode.translateSTE(stackTraceElement);

      if (tuple != null) {
        if ("$main".equals(stackTraceElement.getTarget().getRootNode().getQualifiedName())) continue;
        stackTraceSequence = stackTraceSequence.insertLast(tuple);
      }
    }

    if (throwable.getCause() != null) {
      for (StackTraceElement stackFrame : throwable.getCause().getStackTrace()) {
        stackTraceSequence = stackTraceSequence.insertLast(Tuple.allocate(
            node,
            Seq.fromCharSequence(stackFrame.getClassName()),
            Seq.fromCharSequence(stackFrame.getMethodName()),
            Unit.INSTANCE,
            Unit.INSTANCE
        ));
      }
    }

    return stackTraceSequence;
  }

  public static String stackFrameTupleToString(Tuple tuple) {
    StringBuilder sb = new StringBuilder();
    sb.append(tuple.get(0));
    sb.append(":");
    sb.append(tuple.get(1));
    if (tuple.get(2) != Unit.INSTANCE) {
      sb.append(":");
      sb.append(tuple.get(2));
      sb.append("-");
      sb.append(tuple.get(3));
    }
    return sb.toString();
  }
}
