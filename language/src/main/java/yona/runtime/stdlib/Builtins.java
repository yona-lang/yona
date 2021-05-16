package yona.runtime.stdlib;

import yona.runtime.Context;

import java.util.HashMap;
import java.util.Map;

public class Builtins {
  public final Map<String, StdLibFunction> builtins = new HashMap<>();

  public Builtins(StdLibFunction... stdLibFunctions) {
    for (StdLibFunction stdLibFunction : stdLibFunctions) {
      this.builtins.put(Context.lookupNodeInfo(stdLibFunction.node.getNodeClass()).shortName(), stdLibFunction);
    }
  }

  public void register(StdLibFunction stdLibFunction) {
    this.builtins.put(Context.lookupNodeInfo(stdLibFunction.node.getNodeClass()).shortName(), stdLibFunction);
  }

  public StdLibFunction lookup(Object name) {
    return builtins.get(name);
  }
}
