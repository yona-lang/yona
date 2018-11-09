package abzu.ast.access;

import abzu.ast.ExpressionNode;
import abzu.runtime.Context;
import com.oracle.truffle.api.dsl.*;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.object.DynamicObject;
import abzu.ast.interop.ForeignToAbzuTypeNode;
import abzu.runtime.AbzuUndefinedNameException;

/**
 * The node for reading a property of an object. When executed, this node:
 * <ol>
 * <li>evaluates the object expression on the left hand side of the object access operator</li>
 * <li>evaluated the property name</li>
 * <li>reads the named property</li>
 * </ol>
 */
@NodeInfo(shortName = ".")
@NodeChildren({@NodeChild("receiverNode"), @NodeChild("nameNode")})
@ImportStatic({Context.class, Message.class})
public abstract class ReadPropertyNode extends ExpressionNode {

    @Specialization(guards = "isAbzuObject(receiver)")
    protected Object read(DynamicObject receiver, Object name,
                          @Cached("create()") ReadPropertyCacheNode readNode) {
        /**
         * The polymorphic cache node that performs the actual read. This is a separate node so that
         * it can be re-used in cases where the receiver and name are not nodes but already
         * evaluated values.
         */
        return readNode.executeRead(receiver, name);
    }

    /**
     * Language interoperability: if the receiver object is a foreign value we use Truffle's interop
     * API to access the foreign data.
     */
    @Specialization(guards = "!isAbzuObject(receiver)")
    protected Object readForeign(TruffleObject receiver, Object name,
                                 // The child node to access the foreign object
                                 @Cached("READ.createNode()") Node foreignReadNode,
                                 // The child node to convert the result of the foreign read to a Abzu value
                                 @Cached("create()") ForeignToAbzuTypeNode toAbzuTypeNode) {

        try {
            /* Perform the foreign object access. */
            Object result = ForeignAccess.sendRead(foreignReadNode, receiver, name);
            /* Convert the result to a Abzu value. */
            return toAbzuTypeNode.executeConvert(result);

        } catch (UnknownIdentifierException | UnsupportedMessageException e) {
            /* Foreign access was not successful. */
            throw AbzuUndefinedNameException.undefinedProperty(this, name);
        }
    }

    /**
     * When no specialization fits, the receiver is either not an object (which is a type error), or
     * the object has a shape that has been invalidated.
     */
    @Fallback
    protected Object typeError(@SuppressWarnings("unused") Object r, Object name) {
        /* Non-object types do not have properties. */
        throw AbzuUndefinedNameException.undefinedProperty(this, name);
    }

}
