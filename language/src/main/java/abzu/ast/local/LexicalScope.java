package abzu.ast.local;

import abzu.ast.ExpressionNode;
import abzu.runtime.Unit;
import com.oracle.truffle.api.CompilerDirectives.TruffleBoundary;
import com.oracle.truffle.api.frame.Frame;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeUtil;
import com.oracle.truffle.api.nodes.NodeVisitor;
import com.oracle.truffle.api.nodes.RootNode;
import abzu.ast.expression.LexicalScopeNode;

import java.util.*;

/**
 * AbzuLanguage Language lexical scope. There can be a block scope, or function scope.
 */
public final class LexicalScope {

  private final Node current;
  private final LexicalScopeNode block;
  private final LexicalScopeNode parentBlock;
  private final RootNode root;
  private LexicalScope parent;
  private Map<String, FrameSlot> varSlots;

  /**
   * Create a new block Abzu lexical scope.
   *
   * @param current the current node
   * @param block a nearest block enclosing the current node
   * @param parentBlock a next parent block
   */
  private LexicalScope(Node current, LexicalScopeNode block, LexicalScopeNode parentBlock) {
    this.current = current;
    this.block = block;
    this.parentBlock = parentBlock;
    this.root = null;
  }

  /**
   * Create a new functional Abzu lexical scope.
   *
   * @param current the current node, or <code>null</code> when it would be above the block
   * @param block a nearest block enclosing the current node
   * @param root a functional root node for top-most block
   */
  private LexicalScope(Node current, LexicalScopeNode block, RootNode root) {
    this.current = current;
    this.block = block;
    this.parentBlock = null;
    this.root = root;
  }

  @SuppressWarnings("all") // The parameter node should not be assigned
  public static LexicalScope createScope(Node node) {
    LexicalScopeNode block = getParentBlock(node);
    if (block == null) {
      // We're in the root.
      block = findChildrenBlock(node);
      if (block == null) {
        // Corrupted Abzu AST, no block was found
        return new LexicalScope(null, null, (LexicalScopeNode) null);
      }
      node = null; // node is above the block
    }
    // Test if there is a parent block. If not, we're in the root scope.
    LexicalScopeNode parentBlock = getParentBlock(block);
    if (parentBlock == null) {
      return new LexicalScope(node, block, block.getRootNode());
    } else {
      return new LexicalScope(node, block, parentBlock);
    }
  }

  private static LexicalScopeNode getParentBlock(Node node) {
    LexicalScopeNode block;
    Node parent = node.getParent();
    // Find a nearest block node.
    while (parent != null && !(parent instanceof LexicalScopeNode)) {
      parent = parent.getParent();
    }
    if (parent != null) {
      block = (LexicalScopeNode) parent;
    } else {
      block = null;
    }
    return block;
  }

  private static LexicalScopeNode findChildrenBlock(Node node) {
    LexicalScopeNode[] blockPtr = new LexicalScopeNode[1];
    node.accept(n -> {
      if (n instanceof LexicalScopeNode) {
        blockPtr[0] = (LexicalScopeNode) n;
        return false;
      } else {
        return true;
      }
    });
    return blockPtr[0];
  }

  public LexicalScope findParent() {
    if (parentBlock == null) {
      // This was a root scope.
      return null;
    }
    if (parent == null) {
      Node node = block;
      LexicalScopeNode newBlock = parentBlock;
      // Test if there is a next parent block. If not, we're in the root scope.
      LexicalScopeNode newParentBlock = getParentBlock(newBlock);
      if (newParentBlock == null) {
        parent = new LexicalScope(node, newBlock, newBlock.getRootNode());
      } else {
        parent = new LexicalScope(node, newBlock, newParentBlock);
      }
    }
    return parent;
  }

  /**
   * @return the function name for function scope, "block" otherwise.
   */
  public String getName() {
    if (root != null) {
      return root.getName();
    } else {
      return "block";
    }
  }

  /**
   * @return the node representing the scope, the block node for block scopes and the
   *         {@link RootNode} for functional scope.
   */
  public Node getNode() {
    if (root != null) {
      return root;
    } else {
      return block;
    }
  }

  public Object getVariables(Frame frame) {
    Map<String, FrameSlot> vars = getVars();
    Object[] args = null;
    // Use arguments when the current node is above the block
    if (current == null) {
      args = (frame != null) ? frame.getArguments() : null;
    }
    return new VariablesMapObject(vars, args, frame);
  }

  public Object getArguments(Frame frame) {
    if (root == null) {
      // No arguments for block scope
      return null;
    }
    // The slots give us names of the arguments:
    Map<String, FrameSlot> argSlots = collectArgs(block);
    // The frame's arguments array give us the argument values:
    Object[] args = (frame != null) ? frame.getArguments() : null;
    // Create a TruffleObject having the arguments as properties:
    return new VariablesMapObject(argSlots, args, frame);
  }

  private Map<String, FrameSlot> getVars() {
    if (varSlots == null) {
      if (current != null) {
        varSlots = collectVars(block, current);
      } else if (block != null) {
        // Provide the arguments only when the current node is above the block
        varSlots = collectArgs(block);
      } else {
        varSlots = Collections.emptyMap();
      }
    }
    return varSlots;
  }

  private boolean hasParentVar(String name) {
    LexicalScope p = this;
    while ((p = p.findParent()) != null) {
      if (p.getVars().containsKey(name)) {
        return true;
      }
    }
    return false;
  }

  private Map<String, FrameSlot> collectVars(Node varsBlock, Node currentNode) {
    // Variables are slot-based.
    // To collect declared variables, traverse the block's AST and find slots associated
    // with WriteLocalVariableNode. The traversal stops when we hit the current node.
    Map<String, FrameSlot> slots = new LinkedHashMap<>(4);
    NodeUtil.forEachChild(varsBlock, new NodeVisitor() {
      @Override
      public boolean visit(Node node) {
        if (node == currentNode) {
          return false;
        }
        // Do not enter any nested blocks.
        if (!(node instanceof LexicalScopeNode)) {
          boolean all = NodeUtil.forEachChild(node, this);
          if (!all) {
            return false;
          }
        }
        // Write to a variable is a declaration unless it exists already in a parent scope.
        if (node instanceof WriteLocalVariableNode) {
          WriteLocalVariableNode wn = (WriteLocalVariableNode) node;
          String name = Objects.toString(wn.getSlot().getIdentifier());
          if (!hasParentVar(name)) {
            slots.put(name, wn.getSlot());
          }
        }
        return true;
      }
    });
    return slots;
  }

  private static Map<String, FrameSlot> collectArgs(Node block) {
    // Arguments are pushed to frame slots at the beginning of the function block.
    // To collect argument slots, search for SLReadArgumentNode inside of
    // WriteLocalVariableNode.
    Map<String, FrameSlot> args = new LinkedHashMap<>(4);
    NodeUtil.forEachChild(block, new NodeVisitor() {

      private WriteLocalVariableNode wn; // The current write node containing a slot

      @Override
      public boolean visit(Node node) {
        // When there is a write node, search for SLReadArgumentNode among its children:
        if (node instanceof WriteLocalVariableNode) {
          wn = (WriteLocalVariableNode) node;
          boolean all = NodeUtil.forEachChild(node, this);
          wn = null;
          return all;
        } else if (wn != null && (node instanceof ReadArgumentNode)) {
          FrameSlot slot = wn.getSlot();
          String name = Objects.toString(slot.getIdentifier());
          assert !args.containsKey(name) : name + " argument exists already.";
          args.put(name, slot);
          return true;
        } else if (wn == null && (node instanceof ExpressionNode)) {
          // A different Abzu node - we're done.
          return false;
        } else {
          return NodeUtil.forEachChild(node, this);
        }
      }
    });
    return args;
  }

  @ExportLibrary(InteropLibrary.class)
  static final class VariablesMapObject implements TruffleObject {

    final Map<String, ? extends FrameSlot> slots;
    final Object[] args;
    final Frame frame;

    private VariablesMapObject(Map<String, ? extends FrameSlot> slots, Object[] args, Frame frame) {
      this.slots = slots;
      this.args = args;
      this.frame = frame;
    }

    @SuppressWarnings("static-method")
    @ExportMessage
    boolean hasMembers() {
      return true;
    }

    @ExportMessage
    @TruffleBoundary
    Object getMembers(@SuppressWarnings("unused") boolean includeInternal) {
      return new KeysArray(slots.keySet().toArray(new String[0]));
    }

    @ExportMessage
    @TruffleBoundary
    void writeMember(String member, Object value) throws UnsupportedMessageException, UnknownIdentifierException {
      if (frame == null) {
        throw UnsupportedMessageException.create();
      }
      FrameSlot slot = slots.get(member);
      if (slot == null) {
        throw UnknownIdentifierException.create(member);
      } else {
        Object info = slot.getInfo();
        if (args != null && info != null) {
          args[(Integer) info] = value;
        } else {
          frame.setObject(slot, value);
        }
      }
    }

    @ExportMessage
    @TruffleBoundary
    Object readMember(String member) throws UnknownIdentifierException {
      if (frame == null) {
        return Unit.INSTANCE;
      }
      FrameSlot slot = slots.get(member);
      if (slot == null) {
        throw UnknownIdentifierException.create(member);
      } else {
        Object value;
        Object info = slot.getInfo();
        if (args != null && info != null) {
          value = args[(Integer) info];
        } else {
          value = frame.getValue(slot);
        }
        return value;
      }
    }

    @SuppressWarnings("static-method")
    @ExportMessage
    boolean isMemberInsertable(@SuppressWarnings("unused") String member) {
      return false;
    }

    @ExportMessage
    @TruffleBoundary
    boolean isMemberModifiable(String member) {
      return slots.containsKey(member);
    }

    @ExportMessage
    @TruffleBoundary
    boolean isMemberReadable(String member) {
      return frame == null || slots.containsKey(member);
    }

  }

  @ExportLibrary(InteropLibrary.class)
  static final class KeysArray implements TruffleObject {

    private final String[] keys;

    KeysArray(String[] keys) {
      this.keys = keys;
    }

    @SuppressWarnings("static-method")
    @ExportMessage
    boolean hasArrayElements() {
      return true;
    }

    @ExportMessage
    boolean isArrayElementReadable(long index) {
      return index >= 0 && index < keys.length;
    }

    @ExportMessage
    long getArraySize() {
      return keys.length;
    }

    @ExportMessage
    Object readArrayElement(long index) throws InvalidArrayIndexException {
      if (!isArrayElementReadable(index)) {
        throw InvalidArrayIndexException.create(index);
      }
      return keys[(int) index];
    }

  }
}
