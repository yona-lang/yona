package abzu.runtime;

import abzu.AbzuLanguage;
//import abzu.parser.AbzuParser$;
import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.source.Source;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Manages the mapping from function names to {@link Function function objects}.
 */
public final class FunctionRegistry {

  private final AbzuLanguage language;
  private final Module module = new Module();

  public FunctionRegistry(AbzuLanguage language) {
    this.language = language;
  }

  /**
   * Returns the canonical {@link Function} object for the given name. If it does not exist yet,
   * it is created.
   */
  public Function lookup(String name, boolean createIfNotPresent) {
    Function result = module.functions.get(name);
    if (result == null && createIfNotPresent) {
      result = new Function(language, name);
      module.functions.put(name, result);
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
//    register(AbzuParser$.MODULE$.parseAbzu(language, newFunctions));
  }

  public Function getFunction(String name) {
    return module.functions.get(name);
  }

  /**
   * Returns the sorted list of all functions, for printing purposes only.
   */
  public List<Function> getFunctions() {
    List<Function> result = new ArrayList<>(module.functions.values());
    Collections.sort(result, (f1, f2) -> f1.toString().compareTo(f2.toString()));
    return result;
  }

  public TruffleObject getModule() {
    return module;
  }
}
