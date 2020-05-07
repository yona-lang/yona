package yatta.ast.builtin.modules;


import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.TypesGen;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.*;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.channels.CompletionHandler;
import java.nio.file.OpenOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.HashSet;
import java.util.Set;

@BuiltinModuleInfo(moduleName = "File")
public final class FileBuiltinModule implements BuiltinModule {

  public static final int FILE_READ_BUFFER_SIZE = 64;

  private static Tuple fileTuple(AsynchronousFileChannel fileHandle, byte[] readBuffer, long position) {
    return new Tuple(new NativeObject(fileHandle), readBuffer, position);
  }

  @NodeInfo(shortName = "open")
  abstract static class FileOpenNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    @SuppressWarnings("unchecked")
    public Object open(Seq uri, yatta.runtime.Set modes, @CachedContext(YattaLanguage.class) Context context) {
      Path path = Paths.get(uri.asJavaString(this));

      Object openOptions = openOptions(modes.unwrapPromises(this));
      if (openOptions instanceof Set) {
        return openFile(path, (Set<OpenOption>) openOptions, context);
      } else { // Promise
        return ((Promise) openOptions).map(modesSet -> openFile(path, (Set<OpenOption>) modesSet, context), this);
      }
    }

    private Object openFile(Path path, Set<OpenOption> openOptions, Context context) {
      try {
        AsynchronousFileChannel asynchronousFileChannel = AsynchronousFileChannel.open(path, openOptions, context.ioExecutor);

        return fileTuple(asynchronousFileChannel, null, 0l);
      } catch (IOException e) {
        throw new YattaException(e.getMessage(), this);
      }
    }

    private Object openOptions(Object modes) {
      if (modes instanceof Object[]) {
        return openOptionsFromArray((Object[]) modes);
      } else if (modes instanceof yatta.runtime.Set) {
        return openOptionsFromArray(((yatta.runtime.Set) modes).toArray());
      } else {
        return ((Promise) modes).map(this::openOptions, this);
      }
    }

    private Object openOptionsFromArray(Object[] modes) {
      Set<OpenOption> openOptions = new HashSet<>();
      for (Object el : modes) {
        try {
          openOptions.add(modeSymbolToOpenOption(TypesGen.expectSymbol(el)));
        } catch (UnexpectedResultException e) {
          throw new BadArgException(e, this);
        }
      }
      return openOptions;
    }

    private OpenOption modeSymbolToOpenOption(Symbol symbol) {
      String symbolName = symbol.asString();
      switch (symbolName) {
        case "read":
          return StandardOpenOption.READ;
        case "write":
          return StandardOpenOption.WRITE;
        case "append":
          return StandardOpenOption.APPEND;
        case "truncate_existing":
          return StandardOpenOption.TRUNCATE_EXISTING;
        case "create":
          return StandardOpenOption.CREATE;
        case "create_new":
          return StandardOpenOption.CREATE_NEW;
        case "delete_on_close":
          return StandardOpenOption.DELETE_ON_CLOSE;
        case "sparse":
          return StandardOpenOption.SPARSE;
        case "sync":
          return StandardOpenOption.SYNC;
        case "dsync":
          return StandardOpenOption.DSYNC;
        default:
          throw new BadArgException("Unknown file mode: " + symbol, this);
      }
    }
  }

  @NodeInfo(shortName = "close")
  abstract static class FileCloseNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit close(Tuple fileTuple) {
      AsynchronousFileChannel asynchronousFileChannel = (AsynchronousFileChannel) ((NativeObject) fileTuple.get(0)).getValue();
      try {
        asynchronousFileChannel.close();
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
      return Unit.INSTANCE;
    }
  }

  @NodeInfo(shortName = "readline")
  abstract static class FileReadLineNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise readline(Tuple fileTuple, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary interopLibrary) {
      AsynchronousFileChannel asynchronousFileChannel = (AsynchronousFileChannel) ((NativeObject) fileTuple.get(0)).getValue();
      ByteBuffer buffer = ByteBuffer.allocate(FILE_READ_BUFFER_SIZE);
      long position = (long) fileTuple.get(2);
      Node thisNode = this;
      Promise promise = new Promise(interopLibrary);

      try {
        asynchronousFileChannel.read(buffer, position, buffer, new CompletionHandler<>() {
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
            } else {
              output = new byte[originalOutput.length + length];
              System.arraycopy(originalOutput, 0, output, 0, originalOutput.length);
              pos = originalOutput.length;
            }

            for (int i = 0; i < length; i++) {
              byte b = attachment.get();
              if (b == '\n') {
                promise.fulfil(new Tuple(context.symbol("ok"), Seq.fromCharSequence(new String(output)), fileTuple(asynchronousFileChannel, null, position + i + 1)), thisNode);
                fulfilled = true;
                break;
              } else {
                output[pos++] = b;
              }
            }
            attachment.clear();

            if (!fulfilled) {
              readline(fileTuple(asynchronousFileChannel, output, position + length), context, interopLibrary).map(res -> {
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
      } catch (Exception ex) {
        promise.fulfil(new YattaException(ex.getMessage(), thisNode), thisNode);
      }

      return promise;
    }
  }

  @NodeInfo(shortName = "readfile")
  abstract static class FileReadFileNode extends BuiltinNode {
    private static final class FileReader implements CompletionHandler<Integer, AsynchronousFileChannel> {
      private final Promise promise;
      private final ByteBuffer buffer;
      private final Node node;
      private final Context context;
      private Seq read = Seq.EMPTY;

      public FileReader(final Promise promise, final ByteBuffer buffer, final Node node, final Context context) {
        this.promise = promise;
        this.buffer = buffer;
        this.node = node;
        this.context = context;
      }

      private int pos = 0;

      @Override
      public void completed(Integer result, AsynchronousFileChannel attachment) {
        if (result <= 0) {
          promise.fulfil(new Tuple(context.symbol("ok"), read), node);
          return;
        } else {
          pos += result;
          read = Seq.catenate(read, Seq.fromCharSequence(new String(buffer.array(), 0, result)));
          buffer.clear();
        }
        attachment.read(buffer, pos, attachment, this);
      }

      @Override
      public void failed(Throwable exc, AsynchronousFileChannel attachment) {
        promise.fulfil(new YattaException(exc.getMessage(), node), node);
      }
    }

    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise readfile(Tuple fileTuple, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary interopLibrary) {
      AsynchronousFileChannel asynchronousFileChannel = (AsynchronousFileChannel) ((NativeObject) fileTuple.get(0)).getValue();
      ByteBuffer buffer = ByteBuffer.allocate(FILE_READ_BUFFER_SIZE);
      long position = (long) fileTuple.get(2);
      Promise promise = new Promise(interopLibrary);

      try {
        asynchronousFileChannel.read(buffer, position, asynchronousFileChannel, new FileReader(promise, buffer, this, context));
      } catch (Exception ex) {
        promise.fulfil(new YattaException(ex.getMessage(), this), this);
      }

      return promise;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileOpenNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileCloseNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileReadLineNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileReadFileNodeFactory.getInstance()));
    return builtins;
  }
}
