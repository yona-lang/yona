package abzu.ast.controlflow;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;

/**
 * A node that just executes a list of expressions and returns result of the last one.
 */
@NodeInfo(shortName = "block", description = "The node implementing a source code block")
public final class BlockNode extends ExpressionNode {

    /**
     * The array of child nodes. The annotation {@link com.oracle.truffle.api.nodes.Node.Children
     * Children} informs Truffle that the field contains multiple children. It is a Truffle
     * requirement that the field is {@code final} and an array of nodes.
     */
    @Node.Children
    private final ExpressionNode[] bodyNodes;

    public BlockNode(ExpressionNode[] bodyNodes) {
        this.bodyNodes = bodyNodes;
    }

    /**
     * Execute all child expressions. The annotation {@link ExplodeLoop} triggers full unrolling of
     * the loop during compilation. This allows the {@link ExpressionNode#executeGeneric method of
     * all children to be inlined.
     */
    @Override
    @ExplodeLoop
    public Object executeGeneric(VirtualFrame frame) {
        /*
         * This assertion illustrates that the array length is really a constant during compilation.
         */
        CompilerAsserts.compilationConstant(bodyNodes.length);

        Object res = null;
        for (ExpressionNode statement : bodyNodes) {
            res = statement.executeGeneric(frame);
        }

        return res;
    }
}
