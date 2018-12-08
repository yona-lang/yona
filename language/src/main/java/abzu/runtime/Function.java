package abzu.runtime;

import com.oracle.truffle.api.Assumption;
import com.oracle.truffle.api.RootCallTarget;
import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.utilities.CyclicAssumption;

import java.util.List;

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
 */
public final class Function implements TruffleObject {

  /**
   * The name of the function.
   */
  private final String name;

  /**
   * Number of arguments
   */
  private int cardinality;

  /**
   * Names of arguments
   */
  private List<String> arguments;

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

  public Function(String name, RootCallTarget callTarget, List<String> arguments) {
    this.name = name;
    this.callTarget = callTarget;
    this.callTargetStable = new CyclicAssumption(name);
    this.cardinality = arguments.size();
    this.arguments = arguments;
  }

  public String getName() {
    return name;
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

  @Override
  public ForeignAccess getForeignAccess() {
    return FunctionMessageResolutionForeign.ACCESS;
  }

  public int getCardinality() {
    return cardinality;
  }

  public List<String> getArguments() {
    return arguments;
  }
}
