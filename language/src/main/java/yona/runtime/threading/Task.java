package yona.runtime.threading;

import yona.runtime.Dict;
import yona.runtime.async.Promise;

public final class Task {
  Promise promise;
  ExecutableFunction function;
  Dict localContexts;
}
