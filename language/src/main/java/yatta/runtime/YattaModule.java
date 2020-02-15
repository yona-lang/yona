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
  final Dict records; // <String, String[]>

  public YattaModule(String fqn, List<String> exports, List<Function> functionsList, Dict records) {
    this.fqn = fqn;
    this.exports = exports;

    for (Function fun : functionsList) {
      this.functions.put(fun.getName(), fun);
    }

    this.records = records;
  }

  @Override
  public String toString() {
    StringBuilder recordsSB = new StringBuilder();
    recordsSB.append('{');
    records.fold(recordsSB, (acc, key, val) -> {
      acc.append(key);
      acc.append('=');
      acc.append(Arrays.toString((String[]) val));
      acc.append(", ");
      return acc;
    });
    if(records.size() > 0) {
      recordsSB.deleteCharAt(recordsSB.length() - 1);
      recordsSB.deleteCharAt(recordsSB.length() - 1);
    }
    recordsSB.append('}');

    return "Module{" +
        "fqn=" + fqn +
        ", exports=" + exports +
        ", functions=" + functions +
        ", records=" + recordsSB.toString() +
        "}";
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

    Dict newRecords = records.union(other.records);

    return new YattaModule(fqn, newExports, newFunctions, newRecords);
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
