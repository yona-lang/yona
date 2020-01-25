package yatta.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;

import java.util.*;

@ExportLibrary(InteropLibrary.class)
@SuppressWarnings("static-method")
public final class YattaModule implements TruffleObject {
  final String fqn;
  final List<String> exports;
  final Map<String, Function> functions = new HashMap<>();

  public YattaModule(String fqn, List<String> exports, List<Function> functionsList) {
    this.fqn = fqn;
    this.exports = exports;

    for (Function fun : functionsList) {
      this.functions.put(fun.getName(), fun);
    }
  }

  @Override
  public String toString() {
    return "Module{" +
        "fqn=" + fqn +
        ", exports=" + exports +
        ", functions=" + functions +
        '}';
  }

  public String getFqn() {
    return fqn;
  }

  public List<String> getExports() {
    return exports;
  }

  public Map<String, Function> getFunctions() {
    return functions;
  }

  public YattaModule merge(YattaModule other) {
    List<String> newExports = new ArrayList<>();
    newExports.addAll(exports);
    newExports.addAll(other.exports);

    List<Function> newFunctions = new ArrayList<>();
    newFunctions.addAll(functions.values());
    newFunctions.addAll(other.functions.values());
    return new YattaModule(fqn, newExports, newFunctions);
  }

  @ExportMessage
  boolean hasMembers() {
    return true;
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  Object readMember(String member) {
    return functions.get(member);
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  boolean isMemberReadable(String member) {
    return functions.containsKey(member);
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  Object getMembers(@SuppressWarnings("unused") boolean includeInternal) {
    return functions.keySet().toArray();
  }

  @ExportMessage
  public boolean isString() {
    return true;
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  public String asString() {
    return toString();
  }

  public static boolean isInstance(TruffleObject obj) {
    return obj instanceof YattaModule;
  }
}
