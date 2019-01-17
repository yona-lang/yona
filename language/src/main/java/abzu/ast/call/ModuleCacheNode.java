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
  private Map<String[], Module> moduleCache = new HashMap<>();

  public abstract Module executeLoad(String[] moduleFQNParts);

  @Specialization(guards = {
    "isCached(moduleFQNParts)"
  })
  protected Module loadCached(@SuppressWarnings("unused") String[] moduleFQNParts,
                              @Cached("retrieveModule(moduleFQNParts)") Module module) {

    return module;
  }

  @Specialization(guards = {
    "!isCached(moduleFQNParts)"
  })
  protected Module loadNotCached(@SuppressWarnings("unused") String[] moduleFQNParts,
                                 @Cached("lookupModule(moduleFQNParts)") Module module) {

    moduleCache.put(moduleFQNParts, module);
    return module;
  }

  protected Module retrieveModule(String[] parts) {
    return moduleCache.get(parts);
  }

  protected boolean isCached(String[] parts) {
    return moduleCache.containsKey(parts);
  }

  protected Module lookupModule(String[] parts) {
    try {
      Path path;
      if (parts.length >= 2) {
        String[] pathParts = new String[parts.length - 1];
        System.arraycopy(parts, 1, pathParts, 0, parts.length - 1);
        pathParts[parts.length - 2] = pathParts[parts.length - 2] + "." + AbzuLanguage.ID;
        path = Paths.get(parts[0], pathParts);
      } else {
        path = Paths.get(parts[0]);
      }
      URL url = path.toUri().toURL();

      Source source = Source.newBuilder(AbzuLanguage.ID, url).build();
      CallTarget callTarget = AbzuLanguage.getCurrentContext().parse(source);
      Module module = (Module) callTarget.call();

      if (!Arrays.equals(module.getFqn().toArray(), parts)) {
        throw new AbzuException("Module file " + url.getPath().substring(Paths.get(".").toUri().toURL().getFile().length() - 2) + " has incorrectly defined module as " + module.getFqn(), this);
      }

      return module;
    } catch (IOException e) {
      throw new AbzuException("Unable to load Module " + Arrays.toString(parts) + " due to: " + e.getMessage(), this);
    }
  }
}
