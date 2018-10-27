package abzu;

import abzu.runtime.AbzuUnit;
import com.oracle.truffle.api.dsl.TypeCast;
import com.oracle.truffle.api.dsl.TypeCheck;
import com.oracle.truffle.api.dsl.TypeSystem;
import abzu.runtime.AbzuFunction;

/**
 * The type system of Abzu, as explained in {@link AbzuLanguage}. Based on the {@link TypeSystem}
 * annotation, the Truffle DSL generates the subclass {@link AbzuTypesGen} with type test and type
 * conversion methods for all types. In this class, we only cover types where the automatically
 * generated ones would not be sufficient.
 */
@TypeSystem({long.class, boolean.class, float.class, byte.class, String.class, AbzuFunction.class, AbzuUnit.class})
public abstract class AbzuTypes {

    /**
     * Example of a manually specified type check that replaces the automatically generated type
     * check that the Truffle DSL would generate. For {@link AbzuUnit}, we do not need an
     * {@code instanceof} check, because we know that there is only a {@link AbzuUnit#INSTANCE
     * singleton} instance.
     */
    @TypeCheck(AbzuUnit.class)
    public static boolean isAbzuNone(Object value) {
        return value == AbzuUnit.INSTANCE;
    }

    /**
     * Example of a manually specified type cast that replaces the automatically generated type cast
     * that the Truffle DSL would generate. For {@link AbzuUnit}, we do not need an actual cast,
     * because we know that there is only a {@link AbzuUnit#INSTANCE singleton} instance.
     */
    @TypeCast(AbzuUnit.class)
    public static AbzuUnit asAbzuNone(Object value) {
        assert isAbzuNone(value);
        return AbzuUnit.INSTANCE;
    }
}
