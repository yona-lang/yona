package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Cached;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.Source;
import com.oracle.truffle.js.runtime.util.TRegexUtil;
import com.oracle.truffle.regex.RegexLanguage;
import com.oracle.truffle.regex.RegexObject;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Set;
import yona.runtime.Symbol;
import yona.runtime.exceptions.NoMatchException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Regexp")
public final class RegexpBuiltinModule implements BuiltinModule {
  @CompilerDirectives.TruffleBoundary
  private static Source createRegexSource(String pattern, String flags) {
    String regexStr = "Flavor=ECMAScript" + '/' + pattern + '/' + flags;
    return Source.newBuilder(RegexLanguage.ID, regexStr, regexStr).mimeType(RegexLanguage.MIME_TYPE).internal(true).build();
  }

  @NodeInfo(shortName = "compile")
  abstract static class CompileBuiltin extends BuiltinNode {
    @Specialization
    public Object compile(Seq pattern,
                          Set options) {
      return Context.get(this).getEnv().parseInternal(createRegexSource(pattern.asJavaString(this), optionsToFlags(options))).call();
    }

    @CompilerDirectives.TruffleBoundary
    private String optionsToFlags(Set options) {
      return options.fold(new StringBuilder(), (acc, option) -> {
        if (option instanceof Symbol optionSymbol) {
          switch (optionSymbol.asString()) {
            case "global":
              return acc.append('g');
            case "multiline":
              return acc.append('m');
            case "ignore_case":
              return acc.append('i');
            case "sticky":
              return acc.append('y');
            case "unicode":
              return acc.append('u');
            case "dot_all":
              return acc.append('s');
          }
        }

        throw new NoMatchException(this, option);
      }).toString();
    }
  }

  @NodeInfo(shortName = "exec")
  abstract static class ExecBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Seq exec(Seq input,
                    RegexObject regexObject,
                    @Cached("create()") TRegexUtil.TRegexCompiledRegexAccessor regexAccessor,
                    @Cached("create()") TRegexUtil.TRegexResultAccessor resultAccessor,
                    @Cached("create()") TRegexUtil.TRegexMaterializeResultNode materializeResultNode) {
      String inputString = input.asJavaString(this);

      int lastIndex = 0;
      Seq groups = Seq.EMPTY;

      while (lastIndex >= 0 && lastIndex < inputString.length()) {
        inputString = inputString.substring(lastIndex);
        Object result = regexAccessor.exec(regexObject, inputString, 0);
        if (resultAccessor.isMatch(result)) {
          int resultsCount = regexAccessor.groupCount(regexObject);
          for (int i = 0; i < resultsCount; i++) {
            Object group = materializeResultNode.materializeGroup(result, i, inputString);
            if (group instanceof String) {
              groups = groups.insertLast(Seq.fromCharSequence((String) group));
            }
            lastIndex = resultAccessor.captureGroupEnd(result, i);
          }
        } else {
          break;
        }
      }
      return groups;
    }
  }

  @NodeInfo(shortName = "replace")
  abstract static class ReplaceBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Seq replace(Seq input,
                       Seq replacement,
                       RegexObject regexObject,
                       @Cached("create()") TRegexUtil.TRegexCompiledRegexAccessor regexAccessor,
                       @Cached("create()") TRegexUtil.TRegexResultAccessor resultAccessor,
                       @Cached("create()") TRegexUtil.TRegexMaterializeResultNode materializeResultNode) {
      String inputString = input.asJavaString(this);
      String replacementString = replacement.asJavaString(this).replace("$$", "$");

      int lastIndex = 0;
      StringBuilder sb = new StringBuilder();

      while (lastIndex >= 0 && lastIndex < inputString.length()) {
        inputString = inputString.substring(lastIndex);
        Object result = regexAccessor.exec(regexObject, inputString, 0);
        if (resultAccessor.isMatch(result)) {
          int resultsCount = regexAccessor.groupCount(regexObject);
          for (int i = 0; i < resultsCount; i++) {
            Object group = materializeResultNode.materializeGroup(result, i, inputString);
            int start = resultAccessor.captureGroupStart(result, i);
            int end = resultAccessor.captureGroupEnd(result, i);

            if (lastIndex < start) {
              sb.append(inputString.substring(0, start));
            }

            sb.append(replacementString.replace("$&", group.toString()));
            lastIndex = end;
          }
        } else {
          sb.append(inputString);
          break;
        }
      }
      return Seq.fromCharSequence(sb.toString());
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(RegexpBuiltinModuleFactory.CompileBuiltinFactory.getInstance()),
        new ExportedFunction(RegexpBuiltinModuleFactory.ExecBuiltinFactory.getInstance()),
        new ExportedFunction(RegexpBuiltinModuleFactory.ReplaceBuiltinFactory.getInstance())
    );
  }
}
