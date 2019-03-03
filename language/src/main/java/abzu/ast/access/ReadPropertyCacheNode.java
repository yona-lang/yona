package abzu.ast.access;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.CompilerDirectives.TruffleBoundary;
import com.oracle.truffle.api.dsl.Cached;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.object.DynamicObject;
import com.oracle.truffle.api.object.Location;
import com.oracle.truffle.api.object.Property;
import com.oracle.truffle.api.object.Shape;
import abzu.runtime.UndefinedNameException;

public abstract class ReadPropertyCacheNode extends PropertyCacheNode {

    public abstract Object executeRead(DynamicObject receiver, Object name);

    /**
     * Polymorphic inline cache for a limited number of distinct property names and shapes.
     */
    @Specialization(limit = "CACHE_LIMIT", //
                    guards = {
                                    "namesEqual(cachedName, name)",
                                    "shapeCheck(shape, receiver)"
                    }, //
                    assumptions = {
                                    "shape.getValidAssumption()"
                    })
    protected static Object readCached(DynamicObject receiver, @SuppressWarnings("unused") Object name,
                                       @SuppressWarnings("unused") @Cached("name") Object cachedName,
                                       @Cached("lookupShape(receiver)") Shape shape,
                                       @Cached("lookupLocation(shape, name)") Location location) {

        return location.get(receiver, shape);
    }

    protected Location lookupLocation(Shape shape, Object name) {
        /* Initialization of cached values always happens in a slow path. */
        CompilerAsserts.neverPartOfCompilation();

        Property property = shape.getProperty(name);
        if (property == null) {
            /* Property does not exist. */
            throw UndefinedNameException.undefinedProperty(this, name);
        }

        return property.getLocation();
    }

    /**
     * The generic case is used if the number of shapes accessed overflows the limit of the
     * polymorphic inline cache.
     */
    @TruffleBoundary
    @Specialization(replaces = {"readCached"}, guards = "receiver.getShape().isValid()")
    protected Object readUncached(DynamicObject receiver, Object name) {
        Object result = receiver.get(name);
        if (result == null) {
            /* Property does not exist. */
            throw UndefinedNameException.undefinedProperty(this, name);
        }
        return result;
    }

    @Specialization(guards = "!receiver.getShape().isValid()")
    protected Object updateShape(DynamicObject receiver, Object name) {
        CompilerDirectives.transferToInterpreter();
        receiver.updateShape();
        return readUncached(receiver, name);
    }

    public static ReadPropertyCacheNode create() {
        return ReadPropertyCacheNodeGen.create();
    }

}
