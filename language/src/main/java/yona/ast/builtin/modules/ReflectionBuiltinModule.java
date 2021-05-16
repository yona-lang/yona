package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.*;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

import java.util.Map;
import java.util.stream.Collectors;

@BuiltinModuleInfo(moduleName = "Reflect")
public final class ReflectionBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "modules")
  abstract static class ModulesBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Set modules(@CachedContext(YonaLanguage.class) Context context) {
      return context.builtinModules.builtins.keySet()
          .stream()
          .map(Seq::fromCharSequence)
          .collect(Set.collect());
    }
  }

  @NodeInfo(shortName = "functions")
  abstract static class FunctionsBuiltin extends BuiltinNode {
    @Specialization
    public Dict functions(Seq module, @CachedContext(YonaLanguage.class) Context context) {
      Object res = context.globals.lookup(module.asJavaString(this));
      if (res == Unit.INSTANCE) {
        return Dict.empty();
      } else {
        return functions((YonaModule) res);
      }
    }

    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Dict functions(YonaModule module) {
      return module.getFunctions().entrySet()
          .stream()
          .filter(entry -> module.getExports().contains(entry.getKey()))
          .collect(Collectors.toUnmodifiableMap(
              e -> Seq.fromCharSequence(e.getKey()),
              Map.Entry::getValue
          )).entrySet()
          .stream()
          .collect(Dict.collect());
    }
  }

  @NodeInfo(shortName = "autocomplete")
  abstract static class AutocompleteBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Set modules(Seq prefix, @CachedContext(YonaLanguage.class) Context context) {
      String prefixString = prefix.asJavaString(this);

      if (!prefixString.contains("::")) {
        return context.globals.fold(Set.empty(), (acc, name, module) -> {
          if (((String) name).startsWith(prefixString)) {
            return acc.add(Seq.fromCharSequence((String) name));
          } else {
            return acc;
          }
        });
      } else {
        String[] parts = prefixString.split("::");
        if (parts.length < 2) {
          return context.globals.fold(Set.empty(), (acc, name, module) -> {
            Set funNames = Set.empty();
            if (((String) name).startsWith(parts[0])) {
              for (String funName : ((YonaModule) module).getExports()) {
                funNames = funNames.add(Seq.fromCharSequence(name + "::" + funName));
              }
            }
            return acc.union(funNames);
          });
        } else {
          return context.globals.fold(Set.empty(), (acc, name, module) -> {
            Set funNames = Set.empty();
            if (((String) name).startsWith(parts[0])) {
              for (String funName : ((YonaModule) module).getExports()) {
                if (funName.startsWith(parts[1])) {
                  funNames = funNames.add(Seq.fromCharSequence(name + "::" + funName));
                }
              }
            }
            return acc.union(funNames);
          });
        }
      }
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(ReflectionBuiltinModuleFactory.ModulesBuiltinFactory.getInstance()),
        new ExportedFunction(ReflectionBuiltinModuleFactory.FunctionsBuiltinFactory.getInstance()),
        new ExportedFunction(ReflectionBuiltinModuleFactory.AutocompleteBuiltinFactory.getInstance())
    );
  }
}
