package abzu.runtime;

import abzu.AbzuLanguage;
import com.oracle.truffle.api.Assumption;
import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.utilities.CyclicAssumption;
import abzu.ast.UndefinedFunctionRootNode;

/**
 * Represents a abzu function. On the Truffle level, a callable element is represented by a
 * {@link RootCallTarget call target}. This class encapsulates a call target, and adds version
 * support: functions in Abzu can be redefined, i.e. changed at run time. When a function is
 * redefined, the call target managed by this function object is changed (and {@link #callTarget} is
 * therefore not a final field).
 * <p>
 * Function redefinition is expected to be rare, therefore optimized call nodes want to speculate
 * that the call target is stable. This is possible with the help of a Truffle {@link Assumption}: a
 * call node can keep the call target returned by {@link #getCallTarget()} cached until the
 * assumption returned by {@link #getCallTargetStable()} is valid.
 * <p>
 * The {@link #callTarget} can be {@code null}. To ensure that only one {@link Function} instance
 * per name exists, the {@link FunctionRegistry} creates an instance also when performing name
 * lookup. A function that has been looked up, i.e., used, but not defined, has a call target that
 * encapsulates a {@link UndefinedFunctionRootNode}.
 */
public final class Function implements TruffleObject {

  /**
   * The name of the function.
   */
  private final String name;

  /**
   * The current implementation of this function.
   */
  private RootCallTarget callTarget;

  /**
   * Manages the assumption that the {@link #callTarget} is stable. We use the utility class
   * {@link CyclicAssumption}, which automatically creates a new {@link Assumption} when the old
   * one gets invalidated.
   */
  private final CyclicAssumption callTargetStable;

  protected Function(AbzuLanguage language, String name) {
    this.name = name;
    this.callTarget = Truffle.getRuntime().createCallTarget(new UndefinedFunctionRootNode(language, name));
    this.callTargetStable = new CyclicAssumption(name);
  }

  public String getName() {
    return name;
  }

  protected void setCallTarget(RootCallTarget callTarget) {
    this.callTarget = callTarget;
    /*
     * We have a new call target. Invalidate all code that speculated that the old call target
     * was stable.
     */
    callTargetStable.invalidate();
  }

  public RootCallTarget getCallTarget() {
    return callTarget;
  }

  public Assumption getCallTargetStable() {
    return callTargetStable.getAssumption();
  }

  /**
   * This method is, e.g., called when using a function literal in a string concatenation. So
   * changing it has an effect on AbzuLanguage programs.
   */
  @Override
  public String toString() {
    return name;
  }

  /**
   * In case you want some of your objects to co-operate with other languages, you need to make
   * them implement {@link TruffleObject} and provide additional
   * {@link FunctionMessageResolution foreign access implementation}.
   */
  @Override
  public ForeignAccess getForeignAccess() {
    return FunctionMessageResolutionForeign.ACCESS;
  }
}
