package abzu.ast.interop;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.dsl.TypeSystemReference;
import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.Message;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.nodes.Node;
import abzu.AbzuTypes;
import abzu.runtime.AbzuContext;
import abzu.runtime.AbzuUnit;

/**
 * The node for converting a foreign primitive or boxed primitive value to an SL value.
 */
@TypeSystemReference(AbzuTypes.class)
public abstract class AbzuForeignToAbzuTypeNode extends Node {

  public abstract Object executeConvert(Object value);

  @Specialization
  protected static Object fromObject(Number value) {
    return AbzuContext.fromForeignValue(value);
  }

  @Specialization
  protected static Object fromString(String value) {
    return value;
  }

  @Specialization
  protected static Object fromBoolean(boolean value) {
    return value;
  }

  @Specialization
  protected static Object fromChar(char value) {
    return String.valueOf(value);
  }

  /*
   * In case the foreign object is a boxed primitive we unbox it using the UNBOX message.
   */
  @Specialization(guards = "isBoxedPrimitive(value)")
  public Object unbox(TruffleObject value) {
    Object unboxed = doUnbox(value);
    return AbzuContext.fromForeignValue(unboxed);
  }

  @Specialization(guards = "!isBoxedPrimitive(value)")
  public Object fromTruffleObject(TruffleObject value) {
    return value;
  }

  @Child
  private Node isBoxed;

  protected final boolean isBoxedPrimitive(TruffleObject object) {
    if (isBoxed == null) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      isBoxed = insert(Message.IS_BOXED.createNode());
    }
    return ForeignAccess.sendIsBoxed(isBoxed, object);
  }

  @Child
  private Node unbox;

  protected final Object doUnbox(TruffleObject value) {
    if (unbox == null) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      unbox = insert(Message.UNBOX.createNode());
    }
    try {
      return ForeignAccess.sendUnbox(unbox, value);
    } catch (UnsupportedMessageException e) {
      return AbzuUnit.INSTANCE;
    }
  }

  public static AbzuForeignToAbzuTypeNode create() {
    return AbzuForeignToAbzuTypeNodeGen.create();
  }
}
