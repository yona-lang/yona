package yatta.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Context;
import yatta.runtime.NativeObject;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.JavaException;
import yatta.runtime.exceptions.PolyglotException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Parameter;
import java.util.Arrays;

@BuiltinModuleInfo(moduleName = "Java")
public final class JavaBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "type")
  abstract static class TypeBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object type(Seq name, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      if (!env.isHostLookupAllowed()) {
        throw new PolyglotException("Host lookup is not allowed", this);
      }
      try {
        return env.lookupHostSymbol(name.asJavaString(this));
      } catch (RuntimeException e) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("Host symbol %s is not defined or access has been denied", name.asJavaString(this)), e, this);
      }
    }
  }

  @NodeInfo(shortName = "throw")
  abstract static class RaiseBuiltin extends BuiltinNode {
    @Specialization(guards = {"isForeignObject(object.getValue())", "isThrowable(object.getValue())"})
    @CompilerDirectives.TruffleBoundary
    public Object type(NativeObject object) {
      Throwable throwable = (Throwable) object.getValue();
      throw new JavaException(throwable, this);
    }
  }

  @NodeInfo(shortName = "new")
  abstract static class NewBuiltin extends BuiltinNode {
    @Specialization(guards = {"isForeignObject(klass)"})
    @CompilerDirectives.TruffleBoundary
    public Object newObject(TruffleObject klass, Seq args, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      try {
        Object hostKlass = env.asHostObject(klass);
        Object unwrappedArgs = args.unwrapPromises(this);
        if (unwrappedArgs instanceof Seq) {
          return instantiate(args, ((Class<?>) hostKlass).getConstructors());
        } else {
          Promise unwrappedArgsPromise = (Promise) unwrappedArgs;
          return unwrappedArgsPromise.map((els) -> {
            try {
              return instantiate(Seq.sequence((Object[]) els), ((Class<?>) hostKlass).getConstructors());
            } catch (InstantiationException | IllegalAccessException | InvocationTargetException e) {
              return new PolyglotException(e, this);
            }
          }, this);
        }
      } catch (ClassCastException | InvocationTargetException | InstantiationException | IllegalAccessException e) {
        throw new PolyglotException(e, this);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Object instantiate(Seq args, Constructor<?>[] constructors) throws InstantiationException, IllegalAccessException, InvocationTargetException {
      InteropLibrary interop = InteropLibrary.getFactory().getUncached();
      Object[] javaObjects = new Object[(int) args.length()];
      Constructor<?> constructor = null;
      CONSTRUCTOR:
      for (Constructor<?> c : constructors) {
        if (c.getParameters().length == (int) args.length()) {
          for (int i = 0; i < c.getParameters().length; i++) {
            Parameter parameter = c.getParameters()[i];
            Object hostValue = getHostValue(interop, args.lookup(i, this));
            if (parameter.getType().equals(hostValue.getClass()) || parameter.getType().isInstance(hostValue.getClass())) {
              javaObjects[i] = hostValue;
            } else {
              continue CONSTRUCTOR;
            }
          }
          constructor = c;
        }
      }

      try {
        if (constructor != null) {
          return new NativeObject(constructor.newInstance(javaObjects));
        } else {
          CompilerDirectives.transferToInterpreterAndInvalidate();
          throw new PolyglotException(String.format("No constructor found for arguments: %s", args), this);
        }
      } catch (IllegalArgumentException e) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("Unable to call constructor '%s' using arguments: %s", constructor, Arrays.toString(javaObjects)), this);
      }
    }

    private Object getHostValue(InteropLibrary interop, Object el) {
      try {
        if (interop.fitsInLong(el)) {
          return interop.asLong(el);
        } else if (interop.fitsInDouble(el)) {
          return (long) interop.asDouble(el);
        } else if (interop.isString(el)) {
          return interop.asString(el);
        } else if (interop.isBoolean(el)) {
          return interop.asBoolean(el);
        } else {
          return el;
        }
      } catch (UnsupportedMessageException e) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new AssertionError();
      }
    }
  }

  @NodeInfo(shortName = "instanceof")
  abstract static class InstanceOfBuiltin extends BuiltinNode {
    @Specialization(guards = {"isForeignObject(object.getValue())", "isForeignObject(klass)"})
    public boolean check(NativeObject object, TruffleObject klass, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      try {
        Object hostKlass = env.asHostObject(klass);
        if (hostKlass instanceof Class<?>) {
          return ((Class<?>) hostKlass).isInstance(object.getValue());
        } else {
          return false;
        }
      } catch (ClassCastException cce) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("klass argument '%s' is not a host object", klass), cce, this);
      }
    }

    @Specialization(guards = {"!isForeignObject(object)", "isForeignObject(klass)"})
    public boolean check(Object object, TruffleObject klass, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      try {
        Object hostKlass = env.asHostObject(klass);
        if (hostKlass instanceof Class<?>) {
          return ((Class<?>) hostKlass).isInstance(object);
        } else {
          return false;
        }
      } catch (ClassCastException cce) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("klass argument '%s' is not a host object", klass), cce, this);
      }
    }

    @Specialization(guards = {"isForeignObject(object)", "isForeignObject(klass)"})
    public boolean checkForeign(Object object, TruffleObject klass, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      try {
        Object hostObject = env.asHostObject(object);
        Object hostKlass = env.asHostObject(klass);
        if (hostKlass instanceof Class<?>) {
          return ((Class<?>) hostKlass).isInstance(hostObject);
        } else {
          return false;
        }
      } catch (ClassCastException cce) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("The object klass '%s' arguments is not a host object", klass), cce, this);
      }
    }
  }

  @NodeInfo(shortName = "cast")
  abstract static class CastBuiltin extends BuiltinNode {
    @Specialization(guards = {"isForeignObject(object.getValue())", "isForeignObject(klass)"})
    public Object cast(NativeObject object, TruffleObject klass, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      try {
        Object hostKlass = env.asHostObject(klass);
        if (hostKlass instanceof Class<?>) {
          return new NativeObject(((Class<?>) hostKlass).cast(object.getValue()));
        } else {
          CompilerDirectives.transferToInterpreterAndInvalidate();
          throw new PolyglotException(String.format("klass argument '%s' is not a valid class", klass), this);
        }
      } catch (ClassCastException cce) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("klass argument '%s' is not a host object", klass), cce, this);
      }
    }

    @Specialization(guards = {"!isForeignObject(object)", "isForeignObject(klass)"})
    public Object cast(Object object, TruffleObject klass, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      try {
        Object hostKlass = env.asHostObject(klass);
        if (hostKlass instanceof Class<?>) {
          return new NativeObject(((Class<?>) hostKlass).cast(object));
        } else {
          CompilerDirectives.transferToInterpreterAndInvalidate();
          throw new PolyglotException(String.format("klass argument '%s' is not a valid class", klass), this);
        }
      } catch (ClassCastException cce) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("klass argument '%s' is not a host object", klass), cce, this);
      }
    }

    @Specialization(guards = {"isForeignObject(object)", "isForeignObject(klass)"})
    public Object castForeign(Object object, TruffleObject klass, @CachedContext(YattaLanguage.class) Context context) {
      TruffleLanguage.Env env = context.getEnv();
      try {
        Object hostObject = env.asHostObject(object);
        Object hostKlass = env.asHostObject(klass);
        if (hostKlass instanceof Class<?>) {
          return new NativeObject(((Class<?>) hostKlass).cast(hostObject));
        } else {
          CompilerDirectives.transferToInterpreterAndInvalidate();
          throw new PolyglotException(String.format("klass argument '%s' is not a valid class", klass), this);
        }
      } catch (ClassCastException cce) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new PolyglotException(String.format("The object klass '%s' arguments is not a host object", klass), cce, this);
      }
    }
  }

  @Override
  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(JavaBuiltinModuleFactory.TypeBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(JavaBuiltinModuleFactory.InstanceOfBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(JavaBuiltinModuleFactory.NewBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(JavaBuiltinModuleFactory.RaiseBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(JavaBuiltinModuleFactory.CastBuiltinFactory.getInstance()));
    return builtins;
  }
}
