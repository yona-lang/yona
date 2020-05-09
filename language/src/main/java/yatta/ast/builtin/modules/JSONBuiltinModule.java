package yatta.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import org.antlr.v4.runtime.*;
import yatta.YattaException;
import yatta.ast.builtin.BuiltinNode;
import yatta.json.JSONLexer;
import yatta.json.JSONParser;
import yatta.json.JSONParserVisitor;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

import java.nio.ByteBuffer;

@BuiltinModuleInfo(moduleName = "JSON")
public final class JSONBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "parse")
  abstract static class ParseBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object parse(Seq str) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      JSONLexer lexer = new JSONLexer(seqToCharStream(str));
      TokenStream tokens = new CommonTokenStream(lexer);
      JSONParser parser = new JSONParser(tokens);
      JSONParserVisitor visitor = new JSONParserVisitor();

      return visitor.visit(parser.json());
    }

    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object parse(Promise promise) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      return promise.map(obj -> {
        if (obj instanceof Seq) {
          CompilerDirectives.transferToInterpreterAndInvalidate();
          Seq str = (Seq) obj;
          JSONLexer lexer = new JSONLexer(seqToCharStream(str));
          TokenStream tokens = new CommonTokenStream(lexer);
          JSONParser parser = new JSONParser(tokens);
          JSONParserVisitor visitor = new JSONParserVisitor();

          return visitor.visit(parser.json());
        } else {
          return YattaException.typeError(this, obj);
        }
      }, this);
    }

    private CharStream seqToCharStream(Seq str) {
      CharStream charStream;
      if (str.isString()) {
        charStream = CharStreams.fromString(str.asJavaString(this));
      } else {
        long len = str.length();

        if (len > Integer.MAX_VALUE) {
          throw new BadArgException("Sequence too long to be converted to Java byte array", this);
        }

        ByteBuffer byteBuffer = ByteBuffer.allocate((int) len);
        if (str.asBytes(byteBuffer)) {
          byteBuffer.limit(byteBuffer.position());
          byteBuffer.position(0);
          charStream = CodePointCharStream.fromBuffer(CodePointBuffer.withBytes(byteBuffer));
        } else {
          throw new BadArgException("Provided string is not a valid byte sequence or Yatta string: " + str, this);
        }
      }
      return charStream;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(JSONBuiltinModuleFactory.ParseBuiltinFactory.getInstance()));
    return builtins;
  }
}
