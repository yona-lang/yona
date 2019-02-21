package abzu.ast.call;

import abzu.AbzuException;
import abzu.AbzuLanguage;
import abzu.Types;
import abzu.runtime.Module;
import com.oracle.truffle.api.CallTarget;
import com.oracle.truffle.api.dsl.Cached;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.dsl.TypeSystemReference;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;

import java.io.IOException;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

@NodeInfo
@TypeSystemReference(Types.class)
public abstract class ModuleCacheNode extends Node {
  private Map<String, Module> moduleCache = new HashMap<>();

  public abstract Module executeLoad(String[] packageParts, String moduleName);

  @Specialization(guards = {
      "isCached(FQN)"
  })
  protected Module loadCached(@SuppressWarnings("unused") String[] packageParts,
                              @SuppressWarnings("unused") String moduleName,
                              @SuppressWarnings("unused") @Cached("getFQN(packageParts, moduleName)") String FQN,
                              @Cached("retrieveModule(FQN)") Module module) {

    return module;
  }

  @Specialization(guards = {
      "!isCached(FQN)"
  })
  protected Module loadNotCached(@SuppressWarnings("unused") String[] packageParts,
                                 @SuppressWarnings("unused") String moduleName,
                                 @Cached("getFQN(packageParts, moduleName)") String FQN,
                                 @Cached("lookupModule(packageParts, moduleName, FQN)") Module module) {

    moduleCache.put(FQN, module);
    return module;
  }

  protected Module retrieveModule(String FQN) {
    return moduleCache.get(FQN);
  }

  protected boolean isCached(String FQN) {
    return moduleCache.containsKey(FQN);
  }

  protected Module lookupModule(String[] packageParts, String moduleName, String FQN) {
    try {
      Path path;
      if (packageParts.length > 0) {
        String[] pathParts = new String[packageParts.length];
        System.arraycopy(packageParts, 1, pathParts, 0, packageParts.length - 1);
        pathParts[pathParts.length - 1] = moduleName + "." + AbzuLanguage.ID;
        path = Paths.get(packageParts[0], pathParts);
      } else {
        path = Paths.get(moduleName);
      }
      URL url = path.toUri().toURL();

      Source source = Source.newBuilder(AbzuLanguage.ID, url).build();
      CallTarget callTarget = AbzuLanguage.getCurrentContext().parse(source);
      Module module = (Module) callTarget.call();

      if (!module.getFqn().equals(FQN)) {
        throw new AbzuException("Module file " + url.getPath().substring(Paths.get(".").toUri().toURL().getFile().length() - 2) + " has incorrectly defined module as " + module.getFqn(), this);
      }

      return module;
    } catch (IOException e) {
      throw new AbzuException("Unable to load Module " + FQN + " due to: " + e.getMessage(), this);
    }
  }

  public static String getFQN(String[] packageParts, String moduleName) {
    if (packageParts.length > 0) {
      StringBuilder sb = new StringBuilder();
      for (String packagePart : packageParts) {
        sb.append(packagePart);
        sb.append("\\");
      }
      sb.append(moduleName);
      return sb.toString();
    } else {
      return moduleName;
    }
  }
}
