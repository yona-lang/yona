package yatta.ast.builtin.modules;


import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.*;
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
    @CompilerDirectives.TruffleBoundary
    public Object open(Seq uriSeq, Seq modeSeq) {
      try {
        String uri = uriSeq.asJavaString(this);
        String mode = modeSeq.asJavaString(this);
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
    @CompilerDirectives.TruffleBoundary
    public Promise readline(Tuple fileTuple, @CachedContext(YattaLanguage.class) Context context) {
      AsynchronousFileChannel asynchronousFileChannel = (AsynchronousFileChannel) ((NativeObject) fileTuple.get(0)).getValue();
      ByteBuffer buffer = ByteBuffer.allocate(64);
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
          byte[] originalOutput = (byte[]) fileTuple.get(1);

          byte[] output;
          int pos;

          if (originalOutput == null) {
            output = new byte[length];
            pos = 0;
          }
          else {
            output = new byte[originalOutput.length + length];
            System.arraycopy(originalOutput, 0, output, 0, originalOutput.length);
            pos = originalOutput.length;
          }

          for (int i = 0; i < length; i++) {
            byte b = attachment.get();
            if (b == '\n') {
              promise.fulfil(new Tuple(context.symbol("ok"), Seq.fromCharSequence(new String(output)), new Tuple(new NativeObject(asynchronousFileChannel), null, position + i + 1)), thisNode);
              fulfilled = true;
              break;
            } else {
              output[pos++] = b;
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
