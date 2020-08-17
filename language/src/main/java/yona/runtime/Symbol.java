package yona.runtime;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;

import java.util.Objects;

@ExportLibrary(InteropLibrary.class)
public class Symbol implements TruffleObject {
  private final String name;

  public Symbol(String name) {
    this.name = name;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    Symbol symbol = (Symbol) o;
    return Objects.equals(name, symbol.name);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name);
  }

  @Override
  public String toString() {
    return ":" + name;
  }

  @ExportMessage
  public boolean isString() {
    return true;
  }

  @ExportMessage
  public String asString() {
    return name;
  }
}
