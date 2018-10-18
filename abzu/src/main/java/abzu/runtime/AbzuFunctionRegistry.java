package abzu.runtime;

import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.source.Source;
import abzu.AbzuLanguage;
import abzu.AbzuParser;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Manages the mapping from function names to {@link AbzuFunction function objects}.
 */
public final class AbzuFunctionRegistry {
  private final AbzuLanguage language;
  private final FunctionsObject functionsObject = new FunctionsObject();

  public AbzuFunctionRegistry(AbzuLanguage language) {
    this.language = language;
  }

  /**
   * Returns the canonical {@link AbzuFunction} object for the given name. If it does not exist yet,
   * it is created.
   */
  public AbzuFunction lookup(String name, boolean createIfNotPresent) {
    AbzuFunction result = functionsObject.functions.get(name);
    if (result == null && createIfNotPresent) {
      result = new AbzuFunction(language, name);
      functionsObject.functions.put(name, result);
    }
    return result;
  }

  /**
   * Associates the {@link AbzuFunction} with the given name with the given implementation root
   * node. If the function did not exist before, it defines the function. If the function existed
   * before, it redefines the function and the old implementation is discarded.
   */
  public AbzuFunction register(String name, RootCallTarget callTarget) {
    AbzuFunction function = lookup(name, true);
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
  public List<AbzuFunction> getFunctions() {
    List<AbzuFunction> result = new ArrayList<>(functionsObject.functions.values());
    Collections.sort(result, new Comparator<AbzuFunction>() {
      public int compare(AbzuFunction f1, AbzuFunction f2) {
        return f1.toString().compareTo(f2.toString());
      }
    });
    return result;
  }

  public TruffleObject getFunctionsObject() {
    return functionsObject;
  }
}
