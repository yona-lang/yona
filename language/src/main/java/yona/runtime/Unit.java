package yona.runtime;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;

@ExportLibrary(InteropLibrary.class)
@SuppressWarnings("static-method")
public final class Unit implements TruffleObject {

  /**
   * The canonical value to represent {@code null} in YonaLanguage.
   */
  public static final Unit INSTANCE = new Unit();

  /**
   * Disallow instantiation from outside to ensure that the {@link #INSTANCE} is the only
   * instance.
   */
  private Unit() {
  }

  /**
   * This method is, e.g., called when using the {@code null} value in a string concatenation. So
   * changing it has an effect on YonaLanguage programs.
   */
  @Override
  public String toString() {
    return "()";
  }

  /**
   * {@link Unit} values are interpreted as null values by other languages.
   */
  @ExportMessage
  boolean isNull() {
    return true;
  }
}
