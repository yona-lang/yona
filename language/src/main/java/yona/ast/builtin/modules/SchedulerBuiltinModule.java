package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLogger;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.PrivateFunction;
import yona.runtime.stdlib.util.TimeUnitUtil;
import yona.runtime.threading.ExecutableFunction;

import java.time.Duration;
import java.time.LocalDateTime;
import java.time.temporal.ChronoUnit;
import java.util.concurrent.TimeUnit;
import java.util.logging.Level;

@BuiltinModuleInfo(moduleName = "Scheduler")
public final class SchedulerBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "run_at_fixed_rate")
  abstract static class RunAtFixedRateBuiltin extends BuiltinNode {
    private final TruffleLogger LOGGER = YonaLanguage.getLogger(RunAtFixedRateBuiltin.class);

    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(long initialDelay, Tuple period, Function function, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (function.getCardinality() > 0) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new BadArgException("run_at_* functions accepts only functions with zero arguments. Function " + function + " expects " + function.getCardinality() + "arguments", this);
      }

      Object periodSeconds = TimeUnitUtil.getSeconds(period, this);
      LOGGER.log(Level.FINE, "Scheduling function %s with initial delay: %d and period %s seconds.".formatted(function, initialDelay, periodSeconds));

      if (periodSeconds instanceof Promise delaySecondsPromise) {
        return delaySecondsPromise.map(periodSecondsUnwrapped -> {
          context.schedulerExecutor.scheduleAtFixedRate(() -> {
            LOGGER.fine("Running scheduled function: " + function);
            context.threading.submit(new Promise(dispatch), new ExecutableFunction.YonaExecutableFunction(function, dispatch, this));
          }, initialDelay, (long) periodSecondsUnwrapped, TimeUnit.SECONDS);
          return Unit.INSTANCE;
        }, this);
      } else {
        context.schedulerExecutor.scheduleAtFixedRate(() -> {
          LOGGER.fine("Running scheduled function: " + function);
          context.threading.submit(new Promise(dispatch), new ExecutableFunction.YonaExecutableFunction(function, dispatch, this));
        }, initialDelay, (long) periodSeconds, TimeUnit.SECONDS);
        return Unit.INSTANCE;
      }
    }
  }

  @NodeInfo(shortName = "run_with_fixed_rate")
  abstract static class RunWithFixedRateBuiltin extends BuiltinNode {
    private final TruffleLogger LOGGER = YonaLanguage.getLogger(RunWithFixedRateBuiltin.class);

    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(long initialDelay, Tuple delay, Function function, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (function.getCardinality() > 0) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new BadArgException("run_at_* functions accepts only functions with zero arguments. Function " + function + " expects " + function.getCardinality() + "arguments", this);
      }

      Object delaySeconds = TimeUnitUtil.getSeconds(delay, this);
      LOGGER.log(Level.FINE, "Scheduling function %s with initial delay: %d and period %s seconds.".formatted(function, initialDelay, delaySeconds));

      if (delaySeconds instanceof Promise delaySecondsPromise) {
        return delaySecondsPromise.map(delaySecondsUnwrapped -> {
          context.schedulerExecutor.scheduleWithFixedDelay(() -> {
            LOGGER.fine("Running scheduled function: " + function);
            context.threading.submit(new Promise(dispatch), new ExecutableFunction.YonaExecutableFunction(function, dispatch, this));
          }, initialDelay, (long) delaySecondsUnwrapped, TimeUnit.SECONDS);
          return Unit.INSTANCE;
        }, this);
      } else {
        context.schedulerExecutor.scheduleWithFixedDelay(() -> {
          LOGGER.fine("Running scheduled function: " + function);
          context.threading.submit(new Promise(dispatch), new ExecutableFunction.YonaExecutableFunction(function, dispatch, this));
        }, initialDelay, (long) delaySeconds, TimeUnit.SECONDS);
        return Unit.INSTANCE;
      }
    }
  }

  @NodeInfo(shortName = "seconds_until_next")
  abstract static class SecondsUntilNextBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public long secondsUntilNext(Symbol unit) {
      LocalDateTime start = LocalDateTime.now();
      LocalDateTime end = switch (unit.asString()) {
        case "second" -> start.plusSeconds(1).truncatedTo(ChronoUnit.SECONDS);
        case "minute" -> start.plusMinutes(1).truncatedTo(ChronoUnit.MINUTES);
        case "hour" -> start.plusHours(1).truncatedTo(ChronoUnit.HOURS);
        case "day" -> start.plusDays(1).truncatedTo(ChronoUnit.DAYS);
        case "week" -> start.plusWeeks(1).truncatedTo(ChronoUnit.WEEKS);
        default -> throw new BadArgException("Unknown time unit: " + unit.asString(), this);
      };

      Duration duration = Duration.between(start, end);
      return duration.toSeconds();
    }
  }

  @NodeInfo(shortName = "offset_from_now_in_seconds")
  abstract static class OffsetFromNowInSecondsBuiltin extends BuiltinNode {
    @Specialization
    public Object offsetFromNow(Tuple timeTuple) {
      return TimeUnitUtil.timeTupleToSeconds(timeTuple.unwrapPromises(this), this);
    }
  }

  @Override
  public Builtins builtins() {
    return new Builtins(
        new PrivateFunction(SchedulerBuiltinModuleFactory.RunAtFixedRateBuiltinFactory.getInstance()),
        new PrivateFunction(SchedulerBuiltinModuleFactory.RunWithFixedRateBuiltinFactory.getInstance()),
        new PrivateFunction(SchedulerBuiltinModuleFactory.SecondsUntilNextBuiltinFactory.getInstance()),
        new PrivateFunction(SchedulerBuiltinModuleFactory.OffsetFromNowInSecondsBuiltinFactory.getInstance())
    );
  }
}
