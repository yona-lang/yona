package yatta.runtime;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;

public final class SymbolMap {
  private static ConcurrentMap<String, Symbol> symbols = new ConcurrentHashMap<>();

  public static Symbol symbol(String name) {
    return symbols.computeIfAbsent(name, Symbol::new);
  }
}
