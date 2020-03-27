package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.CommonTokenStream;
import org.antlr.v4.runtime.TokenStream;
import yatta.YattaException;
import yatta.ast.builtin.BuiltinNode;
import yatta.json.JSONLexer;
import yatta.json.JSONParser;
import yatta.json.JSONParserVisitor;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "JSON")
public final class JSONBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "parse")
  abstract static class ParseBuiltin extends BuiltinNode {
    @Specialization
    public Object parse(Seq str) {
      JSONLexer lexer = new JSONLexer(CharStreams.fromString(str.asJavaString(this)));
      TokenStream tokens = new CommonTokenStream(lexer);
      JSONParser parser = new JSONParser(tokens);
      JSONParserVisitor visitor = new JSONParserVisitor();

      return visitor.visit(parser.json());
    }

    @Specialization
    public Object parse(Promise promise) {
      return promise.map(obj -> {
        if (obj instanceof Seq) {
          Seq str = (Seq) obj;
          JSONLexer lexer = new JSONLexer(CharStreams.fromString(str.asJavaString(this)));
          TokenStream tokens = new CommonTokenStream(lexer);
          JSONParser parser = new JSONParser(tokens);
          JSONParserVisitor visitor = new JSONParserVisitor();

          return visitor.visit(parser.json());
        } else {
          return YattaException.typeError(this, obj);
        }
      }, this);
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(JSONBuiltinModuleFactory.ParseBuiltinFactory.getInstance()));
    return builtins;
  }
}
