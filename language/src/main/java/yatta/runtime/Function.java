package yatta.runtime;

import yatta.YattaLanguage;
import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.dsl.Cached;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import com.oracle.truffle.api.nodes.DirectCallNode;
import com.oracle.truffle.api.nodes.IndirectCallNode;
import com.oracle.truffle.api.source.SourceSection;

/**
 * Represents a Yatta function. On the Truffle level, a callable element is represented by a
 * {@link RootCallTarget call target}. This class encapsulates a call target, and adds version
 * support: functions in Yatta can be redefined, i.e. changed at run time. When a function is
 * redefined, the call target managed by this function object is changed (and {@link #callTarget} is
 * therefore not a final field).
 */
@ExportLibrary(InteropLibrary.class)
public final class Function implements TruffleObject {
  public static final int INLINE_CACHE_SIZE = 2;

  private static final TruffleLogger LOG = TruffleLogger.getLogger(YattaLanguage.ID, Function.class);

  /** The name of the function. */
  private final String name;
  private final String moduleFQN;

  private int cardinality;

  /** The current implementation of this function. */
  private RootCallTarget callTarget;

  private boolean unwrapArgumentPromises;

  public Function(String moduleFQN, String name, RootCallTarget callTarget, int cardinality, boolean unwrapArgumentPromises) {
    this.moduleFQN = moduleFQN;
    this.name = name;
    this.callTarget = callTarget;
    this.cardinality = cardinality;
    this.unwrapArgumentPromises = unwrapArgumentPromises;
  }

  public String getModuleFQN() {
    return moduleFQN;
  }

  public String getName() {
    return name;
  }

  public int getCardinality() {
    return cardinality;
  }

  public RootCallTarget getCallTarget() {
    return callTarget;
  }

  /**
   * This method is, e.g., called when using a function literal in a string concatenation. So
   * changing it has an effect on YattaLanguage programs.
   */
  @Override
  public String toString() {
    return name + "/" + cardinality;
  }

  /**
   * {@link Function} instances are always visible as executable to other languages.
   */
  @SuppressWarnings("static-method")
  public SourceSection getDeclaredLocation() {
    return getCallTarget().getRootNode().getSourceSection();
  }

  public boolean isUnwrapArgumentPromises() {
    return unwrapArgumentPromises;
  }

  /**
   * {@link Function} instances are always visible as executable to other languages.
   */
  @SuppressWarnings("static-method")
  @ExportMessage
  boolean isExecutable() {
    return true;
  }

  /**
   * We allow languages to execute this function. We implement the interop execute message that
   * forwards to a function dispatch.
   */
  @ExportMessage
  abstract static class Execute {

    /**
     * Inline cached specialization of the dispatch.
     *
     * <p>
     * Since Yatta is a quite simple language, the benefit of the inline cache seems small: after
     * checking that the actual function to be executed is the same as the cachedFuntion, we can
     * safely execute the cached call target. You can reasonably argue that caching the call
     * target is overkill, since we could just retrieve it via {@code function.getCallTarget()}.
     * However, caching the call target and using a {@link DirectCallNode} allows Truffle to
     * perform method inlining. In addition, in a more complex language the lookup of the call
     * target is usually much more complicated than in YattaLanguage.
     * </p>
     *
     * <p>
     * {@code limit = "INLINE_CACHE_SIZE"} Specifies the limit number of inline cache
     * specialization instantiations.
     * </p>
     * <p>
     * {@code guards = "function.getCallTarget() == cachedTarget"} The inline cache check. Note
     * that cachedTarget is a final field so that the compiler can optimize the check.
     * </p>
     *
     * @see Cached
     * @see Specialization
     *
     * @param function the dynamically provided function
     * @param cachedFunction the cached function of the specialization instance
     * @param callNode the {@link DirectCallNode} specifically created for the
     *            {@link CallTarget} in cachedFunction.
     */
    @Specialization(limit = "INLINE_CACHE_SIZE", //
        guards = "function.getCallTarget() == cachedTarget")
    @SuppressWarnings("unused")
    protected static Object doDirect(Function function, Object[] arguments,
                                     @Cached("function.getCallTarget()") RootCallTarget cachedTarget,
                                     @Cached("create(cachedTarget)") DirectCallNode callNode) {

      /* Inline cache hit, we are safe to execute the cached call target. */
      return callNode.call(arguments);
    }

    /**
     * Slow-path code for a call, used when the polymorphic inline cache exceeded its maximum
     * size specified in <code>INLINE_CACHE_SIZE</code>. Such calls are not optimized any
     * further, e.g., no method inlining is performed.
     */
    @Specialization(replaces = "doDirect")
    protected static Object doIndirect(Function function, Object[] arguments,
                                       @Cached IndirectCallNode callNode) {
      /*
       * SL has a quite simple call lookup: just ask the function for the current call target,
       * and call it.
       */
      return callNode.call(function.getCallTarget(), arguments);
    }
  }
}
