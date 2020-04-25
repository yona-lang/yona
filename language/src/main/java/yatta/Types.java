package yatta;

import com.oracle.truffle.api.dsl.TypeCast;
import com.oracle.truffle.api.dsl.TypeCheck;
import com.oracle.truffle.api.dsl.TypeSystem;
import yatta.runtime.*;
import yatta.runtime.async.Promise;

import java.lang.reflect.Array;

/**
 * The type system of YattaLanguage, as explained in {@link YattaLanguage}. Based on the {@link TypeSystem}
 * annotation, the Truffle DSL generates the subclass {@link TypesGen} with type test and type
 * conversion methods for all types. In this class, we only cover types where the automatically
 * generated ones would not be sufficient.
 */
@TypeSystem({boolean.class, byte.class, long.class, double.class, int.class, String.class, Function.class, Unit.class,
    Tuple.class, YattaModule.class, StringList.class, Seq.class, Dict.class, Set.class, NativeObject.class,
    Symbol.class, Promise.class})
public abstract class Types {

  /**
   * Example of a manually specified type check that replaces the automatically generated type
   * check that the Truffle DSL would generate. For {@link Unit}, we do not need an
   * {@code instanceof} check, because we know that there is only a {@link Unit#INSTANCE
   * singleton} instance.
   */
  @TypeCheck(Unit.class)
  public static boolean isYattaUnit(Object value) {
    return value == Unit.INSTANCE;
  }

  /**
   * Example of a manually specified type cast that replaces the automatically generated type cast
   * that the Truffle DSL would generate. For {@link Unit}, we do not need an actual cast,
   * because we know that there is only a {@link Unit#INSTANCE singleton} instance.
   */
  @TypeCast(Unit.class)
  public static Unit asYattaUnit(Object value) {
    assert isYattaUnit(value);
    return Unit.INSTANCE;
  }

  public static Object ensureNotNull(Object value) {
    if (value == null) {
      return Unit.INSTANCE;
    } else {
      return value;
    }
  }

  public static boolean isForeignObject(Object obj) {
    return !(obj instanceof Boolean || obj instanceof Byte || obj instanceof Long ||
        obj instanceof Double || obj instanceof Integer || obj instanceof String || obj instanceof Function ||
        obj instanceof Unit || obj instanceof Tuple || obj instanceof YattaModule || obj instanceof StringList ||
        obj instanceof Seq || obj instanceof Dict || obj instanceof Set || obj instanceof NativeObject ||
        obj instanceof Symbol || obj instanceof Promise);
  }

  public static Object foreignResultToYattaType(Object result) {
    if (result == null) {
      return Unit.INSTANCE;
    } else if (result.getClass().isArray()) {
      Seq res = Seq.EMPTY;
      for (int i = 0; i < Array.getLength(result); i++) {
        res = res.insertLast(foreignResultToYattaType(Array.get(result, i)));
      }
      return res;
    } else if (result instanceof Integer) {
      return (long) (int) result;
    } else if (result instanceof Float) {
      return (double) (float) result;
    } else if (isForeignObject(result)) {
      return new NativeObject(result);
    } else if (result instanceof Character) {
      return Character.codePointAt(new char[]{((char) result)}, 0);
    } else {
      return result;
    }
  }
}
