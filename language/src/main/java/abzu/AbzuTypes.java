package abzu;

import com.oracle.truffle.api.dsl.TypeCast;
import com.oracle.truffle.api.dsl.TypeCheck;
import com.oracle.truffle.api.dsl.TypeSystem;
import abzu.runtime.AbzuFunction;
import abzu.runtime.AbzuNone;

/**
 * The type system of Abzu, as explained in {@link AbzuLanguage}. Based on the {@link TypeSystem}
 * annotation, the Truffle DSL generates the subclass {@link AbzuTypesGen} with type test and type
 * conversion methods for all types. In this class, we only cover types where the automatically
 * generated ones would not be sufficient.
 */
@TypeSystem({long.class, boolean.class, float.class, byte.class, String.class, AbzuFunction.class, AbzuNone.class})
public abstract class AbzuTypes {

    /**
     * Example of a manually specified type check that replaces the automatically generated type
     * check that the Truffle DAbzu would generate. For {@link AbzuNone}, we do not need an
     * {@code instanceof} check, because we know that there is only a {@link AbzuNone#SINGLETON
     * singleton} instance.
     */
    @TypeCheck(AbzuNone.class)
    public static boolean isAbzuNone(Object value) {
        return value == AbzuNone.SINGLETON;
    }

    /**
     * Example of a manually specified type cast that replaces the automatically generated type cast
     * that the Truffle DAbzu would generate. For {@link AbzuNone}, we do not need an actual cast,
     * because we know that there is only a {@link AbzuNone#SINGLETON singleton} instance.
     */
    @TypeCast(AbzuNone.class)
    public static AbzuNone asAbzuNone(Object value) {
        assert isAbzuNone(value);
        return AbzuNone.SINGLETON;
    }
}
