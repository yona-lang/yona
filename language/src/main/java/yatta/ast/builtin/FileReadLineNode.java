package yatta.ast.builtin;

import yatta.YattaException;
import yatta.runtime.NativeObject;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;
import yatta.runtime.SymbolMap;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.nio.ByteBuffer;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.channels.CompletionHandler;

@NodeInfo(shortName = "freadline")
public abstract class FileReadLineNode extends BuiltinNode {
  @Specialization
  public Promise freadline(Tuple fileTuple) {
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
          promise.fulfil(SymbolMap.symbol("eof"), thisNode);
          return;
        }

        attachment.flip();
        int length = attachment.limit();
        StringBuffer output = (StringBuffer) fileTuple.get(1);

        if (output == null) output = new StringBuffer();

        for (int i = 0; i < length; i++) {
          char ch = ((char) attachment.get());
          if (ch == '\n') {
            promise.fulfil(new Tuple(SymbolMap.symbol("ok"), output.toString(), new Tuple(new NativeObject(asynchronousFileChannel), null, position + i + 1)), thisNode);
            fulfilled = true;
            break;
          } else {
            output.append(ch);
          }
        }
        attachment.clear();

        if (!fulfilled) {
          freadline(new Tuple(new NativeObject(asynchronousFileChannel), output, position + length)).map(res -> {
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
