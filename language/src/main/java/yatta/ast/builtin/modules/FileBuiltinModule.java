package yatta.ast.builtin.modules;


import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Builtins;
import yatta.runtime.Context;
import yatta.runtime.NativeObject;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.channels.CompletionHandler;
import java.nio.file.OpenOption;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.List;

@BuiltinModuleInfo(moduleName = "File")
public final class FileBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "open")
  abstract static class FileOpenNode extends BuiltinNode {
    @Specialization
    public Object open(String uri, String mode) {
      try {
        List<OpenOption> openOptions = new ArrayList<>();
        if (mode.equals("r")) {
          openOptions.add(StandardOpenOption.READ);
        }

        AsynchronousFileChannel asynchronousFileChannel = AsynchronousFileChannel.open(Paths.get(uri), openOptions.toArray(new OpenOption[]{}));

        return new Tuple(new NativeObject(asynchronousFileChannel), null, 0l);
      } catch (IOException e) {
        throw new YattaException(e.getMessage(), this);
      }
    }
  }

  @NodeInfo(shortName = "readline")
  abstract static class FileReadLineNode extends BuiltinNode {
    @Specialization
    public Promise readline(Tuple fileTuple, @CachedContext(YattaLanguage.class) Context context) {
      AsynchronousFileChannel asynchronousFileChannel = (AsynchronousFileChannel) ((NativeObject) fileTuple.get(0)).getValue();
      ByteBuffer buffer = ByteBuffer.allocate(1024 * 10);
      long position = (long) fileTuple.get(2);
      Node thisNode = this;
      Promise promise = new Promise();

      asynchronousFileChannel.read(buffer, position, buffer, new CompletionHandler<Integer, ByteBuffer>() {
        @Override
        public void completed(Integer result, ByteBuffer attachment) {
          boolean fulfilled = false;
          if (result <= 0) {
            promise.fulfil(context.symbol("eof"), thisNode);
            return;
          }

          attachment.flip();
          int length = attachment.limit();
          StringBuffer output = (StringBuffer) fileTuple.get(1);

          if (output == null) output = new StringBuffer();

          for (int i = 0; i < length; i++) {
            char ch = ((char) attachment.get());
            if (ch == '\n') {
              promise.fulfil(new Tuple(context.symbol("ok"), output.toString(), new Tuple(new NativeObject(asynchronousFileChannel), null, position + i + 1)), thisNode);
              fulfilled = true;
              break;
            } else {
              output.append(ch);
            }
          }
          attachment.clear();

          if (!fulfilled) {
            readline(new Tuple(new NativeObject(asynchronousFileChannel), output, position + length), context).map(res -> {
              promise.fulfil(res, thisNode);
              return res;
            }, thisNode);
          }
        }

        @Override
        public void failed(Throwable exc, ByteBuffer attachment) {
          promise.fulfil(new YattaException(exc.getMessage(), thisNode), thisNode);
        }
      });

      return promise;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(FileBuiltinModuleFactory.FileOpenNodeFactory.getInstance());
    builtins.register(FileBuiltinModuleFactory.FileReadLineNodeFactory.getInstance());
    return builtins;
  }
}
