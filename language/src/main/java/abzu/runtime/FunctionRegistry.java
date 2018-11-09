package abzu.runtime;

import abzu.AbzuLanguage;
import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.source.Source;
import abzu.AbzuParser;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Manages the mapping from function names to {@link Function function objects}.
 */
public final class FunctionRegistry {
  private final AbzuLanguage language;
  private final FunctionsObject functionsObject = new FunctionsObject();

  public FunctionRegistry(AbzuLanguage language) {
    this.language = language;
  }

  /**
   * Returns the canonical {@link Function} object for the given name. If it does not exist yet,
   * it is created.
   */
  public Function lookup(String name, boolean createIfNotPresent) {
    Function result = functionsObject.functions.get(name);
    if (result == null && createIfNotPresent) {
      result = new Function(language, name);
      functionsObject.functions.put(name, result);
    }
    return result;
  }

  /**
   * Associates the {@link Function} with the given name with the given implementation root
   * node. If the function did not exist before, it defines the function. If the function existed
   * before, it redefines the function and the old implementation is discarded.
   */
  public Function register(String name, RootCallTarget callTarget) {
    Function function = lookup(name, true);
    function.setCallTarget(callTarget);
    return function;
  }

  public void register(RootCallTarget rootCallTarget) {
    register("root", rootCallTarget);
  }

  public void register(Source newFunctions) {
    register(AbzuParser.parseAbzu(language, newFunctions));
  }

  /**
   * Returns the sorted list of all functions, for printing purposes only.
   */
  public List<Function> getFunctions() {
    List<Function> result = new ArrayList<>(functionsObject.functions.values());
    result.sort(Comparator.comparing(Function::toString));
    return result;
  }

  public TruffleObject getFunctionsObject() {
    return functionsObject;
  }
}
