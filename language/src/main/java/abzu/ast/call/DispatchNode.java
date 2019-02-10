package abzu.ast.call;

import abzu.Types;
import abzu.ast.interop.ForeignToAbzuTypeNode;
import abzu.ast.interop.ForeignToAbzuTypeNodeGen;
import abzu.runtime.AbzuUndefinedNameException;
import abzu.runtime.Function;
import com.oracle.truffle.api.Assumption;
import com.oracle.truffle.api.CallTarget;
import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.dsl.*;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.DirectCallNode;
import com.oracle.truffle.api.nodes.IndirectCallNode;
import com.oracle.truffle.api.nodes.Node;

@ReportPolymorphism
@TypeSystemReference(Types.class)
public abstract class DispatchNode extends Node {

  public static final int INLINE_CACHE_SIZE = 2;

  public abstract Object executeDispatch(Object function, Object[] arguments);

  /**
   * Inline cached specialization of the dispatch.
   *
   * <p>
   * Since AbzuLanguage is a quite simple language, the benefit of the inline cache seems small: after
   * checking that the actual function to be executed is the same as the cachedFuntion, we can
   * safely execute the cached call target. You can reasonably argue that caching the call target
   * is overkill, since we could just retrieve it via {@code function.getCallTarget()}. However,
   * caching the call target and using a {@link DirectCallNode} allows Truffle to perform method
   * inlining. In addition, in a more complex language the lookup of the call target is usually
   * much more complicated than in AbzuLanguage.
   * </p>
   *
   * <p>
   * {@code limit = "INLINE_CACHE_SIZE"} Specifies the limit number of inline cache specialization
   * instantiations.
   * </p>
   * <p>
   * {@code guards = "function.getCallTarget() == cachedTarget"} The inline cache check. Note that
   * cachedTarget is a final field so that the compiler can optimize the check.
   * </p>
   * <p>
   * {@code assumptions = "callTargetStable"} Support for function redefinition: When a function
   * is redefined, the call target maintained by the Function object is changed. To avoid a
   * check for that, we use an Assumption that is invalidated by the Function when the change is
   * performed. Since checking an assumption is a no-op in compiled code, the assumption check
   * performed by the DSL does not add any overhead during optimized execution.
   * </p>
   *
   * @see Cached
   * @see Specialization
   *
   * @param function the dynamically provided function
   * @param cachedTarget the cached function of the specialization instance
   * @param callNode the {@link DirectCallNode} specifically created for the {@link CallTarget} in
   *            cachedFunction.
   */
  @Specialization(limit = "INLINE_CACHE_SIZE", //
      guards = "function.getCallTarget() == cachedTarget", //
      assumptions = "callTargetStable")
  @SuppressWarnings("unused")
  protected static Object doDirect(Function function, Object[] arguments,
                                   @Cached("function.getCallTargetStable()") Assumption callTargetStable,
                                   @Cached("function.getCallTarget()") RootCallTarget cachedTarget,
                                   @Cached("create(cachedTarget)") DirectCallNode callNode) {

    /* Inline cache hit, we are safe to execute the cached call target. */
    return callNode.call(arguments);
  }

  /**
   * Slow-path code for a call, used when the polymorphic inline cache exceeded its maximum size
   * specified in <code>INLINE_CACHE_SIZE</code>. Such calls are not optimized any further, e.g.,
   * no method inlining is performed.
   */
  @Specialization(replaces = "doDirect")
  protected static Object doIndirect(Function function, Object[] arguments,
                                     @Cached("create()") IndirectCallNode callNode) {
    /*
     * AbzuLanguage has a quite simple call lookup: just ask the function for the current call target, and
     * call it.
     */
    return callNode.call(function.getCallTarget(), arguments);
  }

  /**
   * When no specialization fits, the receiver is not an object (which is a type error).
   */
  @Fallback
  protected Object unknownFunction(Object function, @SuppressWarnings("unused") Object[] arguments) {
    throw AbzuUndefinedNameException.undefinedFunction(this, function);
  }

  /**
   * Language interoperability: If the function is a foreign value, i.e., not a Function, we use
   * Truffle's interop API to execute the foreign function.
   */
  @Specialization(guards = "isForeignFunction(function)")
  protected Object doForeign(TruffleObject function, Object[] arguments,
                             // The child node to call the foreign function
                             @Cached("createCrossLanguageCallNode(arguments)") Node crossLanguageCallNode,
                             // The child node to convert the result of the foreign call to a AbzuLanguage value
                             @Cached("createToAbzuTypeNode()") ForeignToAbzuTypeNode toAbzuTypeNode) {

    try {
      /* Perform the foreign function call. */
      Object res = ForeignAccess.sendExecute(crossLanguageCallNode, function, arguments);
      /* Convert the result to a AbzuLanguage value. */
      return toAbzuTypeNode.executeConvert(res);

    } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
      /* Foreign access was not successful. */
      throw AbzuUndefinedNameException.undefinedFunction(this, function);
    }
  }

  protected static boolean isForeignFunction(TruffleObject function) {
    return !(function instanceof Function);
  }

  protected static Node createCrossLanguageCallNode(Object[] arguments) {
    return Message.createExecute(arguments.length).createNode();
  }

  protected static ForeignToAbzuTypeNode createToAbzuTypeNode() {
    return ForeignToAbzuTypeNodeGen.create();
  }
}
