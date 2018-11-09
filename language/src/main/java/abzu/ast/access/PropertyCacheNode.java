package abzu.ast.access;

import abzu.Types;
import abzu.runtime.Context;
import abzu.runtime.Function;
import abzu.runtime.Unit;
import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.dsl.TypeSystemReference;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.object.DynamicObject;
import com.oracle.truffle.api.object.Shape;

@TypeSystemReference(Types.class)
public abstract class PropertyCacheNode extends Node {
    protected static final int CACHE_LIMIT = 3;

    protected static boolean shapeCheck(Shape shape, DynamicObject receiver) {
        return shape != null && shape.check(receiver);
    }

    protected static Shape lookupShape(DynamicObject receiver) {
        CompilerAsserts.neverPartOfCompilation();
        assert Context.isAbzuObject(receiver);
        return receiver.getShape();
    }

    /**
     * Property names can be arbitrary abzu objects. We could call {@link Object#equals}, but that is
     * generally a bad idea and therefore discouraged in Truffle.{@link Object#equals} is a virtual
     * call that can call possibly large methods that we do not want in compiled code. For example,
     * we do not want SLBigNumber.equals in compiled code but behind a
     * {@link com.oracle.truffle.api.CompilerDirectives.TruffleBoundary). Therfore, we check types individually. The checks are semantically
     * the same as SLEqualNode.
     * <p>
     * Note that the {@code cachedName} is always a constant during compilation. Therefore, compiled
     * code is always reduced to a single {@code if} that only checks whether the {@code name} has
     * the same type.
     *
     */
    protected static boolean namesEqual(Object cachedName, Object name) {
        if (cachedName instanceof Long && name instanceof Long) {
            return (long) cachedName == (long) name;
        } else if (cachedName instanceof Boolean && name instanceof Boolean) {
            return (boolean) cachedName == (boolean) name;
        } else if (cachedName instanceof String && name instanceof String) {
            return ((String) cachedName).equals(name);
        } else if (cachedName instanceof Function && name instanceof Function) {
            return cachedName == name;
        } else if (cachedName instanceof Unit && name instanceof Unit) {
            return cachedName == name;
        } else {
            assert !cachedName.equals(name);
            return false;
        }
    }

}
