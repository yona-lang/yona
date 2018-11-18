package abzu;

import abzu.runtime.*;
import abzu.runtime.Module;
import com.oracle.truffle.api.dsl.TypeCast;
import com.oracle.truffle.api.dsl.TypeCheck;
import com.oracle.truffle.api.dsl.TypeSystem;

/**
 * The type system of AbzuLanguage, as explained in {@link AbzuLanguage}. Based on the {@link TypeSystem}
 * annotation, the Truffle DSL generates the subclass {@link TypesGen} with type test and type
 * conversion methods for all types. In this class, we only cover types where the automatically
 * generated ones would not be sufficient.
 */
@TypeSystem({boolean.class, byte.class, long.class, double.class, String.class, Function.class, Unit.class, Tuple.class, Module.class})
public abstract class Types {

  /**
   * Example of a manually specified type check that replaces the automatically generated type
   * check that the Truffle DSL would generate. For {@link Unit}, we do not need an
   * {@code instanceof} check, because we know that there is only a {@link Unit#INSTANCE
   * singleton} instance.
   */
  @TypeCheck(Unit.class)
  public static boolean isAbzuUnit(Object value) {
    return value == Unit.INSTANCE;
  }

  /**
   * Example of a manually specified type cast that replaces the automatically generated type cast
   * that the Truffle DSL would generate. For {@link Unit}, we do not need an actual cast,
   * because we know that there is only a {@link Unit#INSTANCE singleton} instance.
   */
  @TypeCast(Unit.class)
  public static Unit asAbzuUnit(Object value) {
    assert isAbzuUnit(value);
    return Unit.INSTANCE;
  }
}
