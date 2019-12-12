package yatta.ast.controlflow;

import yatta.ast.ExpressionNode;
import com.oracle.truffle.api.nodes.BlockNode;
import com.oracle.truffle.api.nodes.BlockNode.ElementExecutor;
import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;

/**
 * A node that just executes a list of expressions and returns result of the last one.
 */
@NodeInfo(shortName = "block", description = "The node implementing a source code block")
public final class YattaBlockNode extends ExpressionNode implements BlockNode.ElementExecutor<ExpressionNode> {
    @Child private BlockNode<ExpressionNode> block;

    public YattaBlockNode(ExpressionNode[] elements) {
        this.block = BlockNode.create(elements, this);
    }

    @Override
    public String toString() {
        return "YattaBlockNode{" +
            "block=" + block +
            '}';
    }

    @Override
    public void executeVoid(VirtualFrame frame, ExpressionNode node, int index, int arg) {
        node.executeGeneric(frame);
    }

    @Override
    public Object executeGeneric(VirtualFrame frame, ExpressionNode node, int index, int arg) {
        return node.executeGeneric(frame);
    }

    @Override
    public Object executeGeneric(VirtualFrame frame) {
        return block.executeGeneric(frame, 0);
    }
}
