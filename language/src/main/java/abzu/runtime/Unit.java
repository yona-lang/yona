package abzu.runtime;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.TruffleObject;

public final class Unit implements TruffleObject {
  /**
   * The canonical value to represent {@code null} in AbzuLanguage.
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
   * changing it has an effect on AbzuLanguage programs.
   */
  @Override
  public String toString() {
    return "()";
  }

  /**
   * In case you want some of your objects to co-operate with other languages, you need to make
   * them implement {@link TruffleObject} and provide additional {@link IsNoneMessageResolution
   * foreign access implementation}.
   */
  @Override
  public ForeignAccess getForeignAccess() {
    return IsNoneMessageResolutionForeign.ACCESS;
  }
}
