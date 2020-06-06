package yatta.ast.builtin.modules;


import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleFile;
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

import java.io.File;
import java.io.IOException;
import java.nio.BufferOverflowException;
import java.nio.ByteBuffer;
import java.nio.channels.AsynchronousFileChannel;
import java.nio.channels.CompletionHandler;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.Collection;
import java.util.HashSet;
import java.util.Set;

@BuiltinModuleInfo(moduleName = "File")
public final class FileBuiltinModule implements BuiltinModule {

  protected static final int FILE_READ_BUFFER_SIZE = 4096;

  protected static final class FileTuple extends Tuple {
    public FileTuple(AsynchronousFileChannel fileHandle, Object readBuffer, long position, Seq additionalOptions, Seq path) {
      this.items = new Object[]{
          new NativeObject(fileHandle), readBuffer, position, additionalOptions, path
      };
    }

    public FileTuple copy(Object readBuffer, long position) {
      return new FileTuple(fileHandle(), readBuffer, position, additionalOptions(), path());
    }

    public FileTuple seek(long position) {
      return new FileTuple(fileHandle(), readBuffer(), position, additionalOptions(), path());
    }

    public AsynchronousFileChannel fileHandle() {
      return (AsynchronousFileChannel) ((NativeObject) items[0]).getValue();
    }

    public Object readBuffer() {
      return items[1];
    }

    public long position() {
      return (long) items[2];
    }

    public Seq additionalOptions() {
      return (Seq) items[3];
    }

    public Seq path() {
      return (Seq) items[4];
    }
  }


  protected static final class FileOptions {
    final Set<OpenOption> openOptions = new HashSet<>();
    Seq additionalOptions = Seq.EMPTY;

    public void addAdditionalOption(Object option) {
      this.additionalOptions = this.additionalOptions.insertFirst(option);
    }
  }

  private abstract static class NewFileNode extends BuiltinNode {
    protected Object openFile(Path path, FileOptions fileOptions, Context context) {
      try {
        AsynchronousFileChannel asynchronousFileChannel = AsynchronousFileChannel.open(path, fileOptions.openOptions, context.ioExecutor);

        return new FileTuple(asynchronousFileChannel, Unit.INSTANCE, 0L, fileOptions.additionalOptions, Seq.fromCharSequence(path.toString()));
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }

    protected Object openOptions(Object modes) {
      if (modes instanceof Object[]) {
        return openOptionsFromArray((Object[]) modes);
      } else if (modes instanceof yatta.runtime.Set) {
        return openOptionsFromArray(((yatta.runtime.Set) modes).toArray());
      } else {
        return ((Promise) modes).map(this::openOptions, this);
      }
    }

    private FileOptions openOptionsFromArray(Object[] modes) {
      FileOptions fileOptions = new FileOptions();
      for (Object el : modes) {
        try {
          Symbol symbol = TypesGen.expectSymbol(el);
          if (isModeAdditionalOption(symbol)) {
            fileOptions.addAdditionalOption(symbol);
          } else {
            fileOptions.openOptions.add(modeSymbolToOpenOption(symbol));
          }
        } catch (UnexpectedResultException e) {
          throw new BadArgException(e, this);
        }
      }
      return fileOptions;
    }

    private boolean isModeAdditionalOption(Symbol symbol) {
      String symbolName = symbol.asString();
      switch (symbolName) {
        case "binary":
          return true;
        default:
          return false;
      }
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

    protected Object openPath(Path path, yatta.runtime.Set modes, Context context) {
      Object fileOptionsObj = openOptions(modes.unwrapPromises(this));
      if (fileOptionsObj instanceof FileOptions) {
        return openFile(path, (FileOptions) fileOptionsObj, context);
      } else { // Promise
        return ((Promise) fileOptionsObj).map(fileOptions -> openFile(path, (FileOptions) fileOptions, context), this);
      }
    }
  }

  @NodeInfo(shortName = "open")
  abstract static class FileOpenNode extends NewFileNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    @SuppressWarnings("unchecked")
    public Object open(Seq uri, yatta.runtime.Set modes, @CachedContext(YattaLanguage.class) Context context) {
      Path path = Paths.get(uri.asJavaString(this));
      return openPath(path, modes, context);
    }
  }

  @NodeInfo(shortName = "close")
  abstract static class FileCloseNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit close(FileTuple fileTuple) {
      try {
        fileTuple.fileHandle().close();
        return Unit.INSTANCE;
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "path")
  abstract static class FilePathNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Seq path(FileTuple fileTuple) {
      return fileTuple.path();
    }
  }

  @NodeInfo(shortName = "delete")
  abstract static class FileDeleteNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit path(FileTuple fileTuple) {
      try {
        Files.deleteIfExists(Paths.get(fileTuple.path().asJavaString(this)));
        return Unit.INSTANCE;
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "seek")
  abstract static class FileSeekNode extends NewFileNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    @SuppressWarnings("unchecked")
    public FileTuple open(FileTuple fileTuple, long position) {
      return fileTuple.seek(position);
    }
  }

  @NodeInfo(shortName = "make_temp")
  abstract static class CreateTempFileNode extends NewFileNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    @SuppressWarnings("unchecked")
    public Object open(Seq prefix, Seq suffix, yatta.runtime.Set modes, @CachedContext(YattaLanguage.class) Context context) {
      try {
        File tempFile = File.createTempFile(prefix.asJavaString(this), suffix.asJavaString(this));
        return openPath(tempFile.toPath(), modes, context);
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "list_dir")
  abstract static class FileListNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Seq path(Seq path, @CachedContext(YattaLanguage.class) Context context) {
      try {
        Collection<TruffleFile> files = context.getEnv().getPublicTruffleFile(path.asJavaString(this)).list();
        Seq ret = Seq.EMPTY;
        for (TruffleFile file : files) {
          ret = ret.insertLast(Seq.fromCharSequence(file.getPath()));
        }
        return ret;
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "read_line")
  abstract static class FileReadLineNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise readline(FileTuple fileTuple, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary interopLibrary) {
      AsynchronousFileChannel asynchronousFileChannel = (AsynchronousFileChannel) ((NativeObject) fileTuple.get(0)).getValue();
      ByteBuffer buffer = ByteBuffer.allocate(FILE_READ_BUFFER_SIZE);
      Node thisNode = this;
      Promise promise = new Promise(interopLibrary);

      try {
        asynchronousFileChannel.read(buffer, fileTuple.position(), buffer, new CompletionHandler<>() {
          @Override
          public void completed(Integer result, ByteBuffer attachment) {
            boolean fulfilled = false;
            if (result <= 0) {
              promise.fulfil(context.symbol("eof"), thisNode);
              return;
            }

            attachment.flip();
            int length = attachment.limit();

            ByteBuffer output;

            if (fileTuple.readBuffer() instanceof ByteBuffer) {
              output = (ByteBuffer) fileTuple.readBuffer();
              output.flip();
            } else {
              output = ByteBuffer.allocate(length);
            }

            for (int i = 0; i < length; i++) {
              byte b = attachment.get();
              if (b == '\r') {
                continue;
              }
              if (b == '\n') {
                promise.fulfil(new Tuple(context.symbol("ok"), bytesToSeq(output, fileTuple.additionalOptions(), context, thisNode), fileTuple.copy(null, fileTuple.position() + i + 1)), thisNode);
                fulfilled = true;
                break;
              } else {
                output.put(b);
              }
            }
            attachment.clear();

            if (!fulfilled) {
              readline(fileTuple.copy(output, fileTuple.position() + length), context, interopLibrary).map(res -> {
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
        promise.fulfil(new yatta.runtime.exceptions.IOException(ex.getMessage(), thisNode), thisNode);
      }

      return promise;
    }

    private static Seq bytesToSeq(ByteBuffer byteBuffer, Seq additionalFileOptions, Context context, Node caller) {
      byteBuffer.flip();
      if (additionalFileOptions.contains(context.symbol("binary"), caller)) {
        return Seq.fromByteBuffer(byteBuffer);
      } else {
        return Seq.fromCharSequence(StandardCharsets.UTF_8.decode(byteBuffer));
      }
    }
  }

  @NodeInfo(shortName = "read")
  abstract static class FileReadFileNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise readfile(FileTuple fileTuple, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary interopLibrary) {
      final class CatenateCompletionHandler implements CompletionHandler<Integer, ByteBuffer> {
        final AsynchronousFileChannel channel;
        final Promise promise;
        long position;
        Seq seq = Seq.EMPTY;

        CatenateCompletionHandler(AsynchronousFileChannel channel, Promise promise, long position) {
          this.channel = channel;
          this.promise = promise;
          this.position = position;
        }

        @Override
        public void completed(Integer result, ByteBuffer attachment) {
          if (result <= 0) {
            promise.fulfil(seq, FileReadFileNode.this);
          } else {
            attachment.flip();

            if (fileTuple.additionalOptions().contains(context.symbol("binary"), FileReadFileNode.this)) {
              seq = Seq.catenate(seq, Seq.fromByteBuffer(attachment));
            } else {
              seq = Seq.catenate(seq, Seq.fromCharSequence(StandardCharsets.UTF_8.decode(attachment)));
            }
            attachment.clear();

            position += result;
            channel.read(attachment, position, attachment, this);
          }
        }

        @Override
        public void failed(Throwable exc, ByteBuffer attachment) {
          promise.fulfil(new yatta.runtime.exceptions.IOException(exc.getMessage(), FileReadFileNode.this), FileReadFileNode.this);
        }
      }

      final ByteBuffer buffer = ByteBuffer.allocate(FILE_READ_BUFFER_SIZE);
      final Promise promise = new Promise(interopLibrary);

      try {
        fileTuple.fileHandle().read(buffer, fileTuple.position(), buffer, new CatenateCompletionHandler(fileTuple.fileHandle(), promise, fileTuple.position()));
      } catch (Exception ex) {
        promise.fulfil(new YattaException(ex.getMessage(), this), this);
      }

      return promise;
    }
  }

  @NodeInfo(shortName = "write")
  abstract static class FileWriteFileNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Promise writefile(FileTuple fileTuple, Seq data, @CachedLibrary(limit = "3") InteropLibrary interopLibrary) {
      final class WriteCompletionHandler implements CompletionHandler<Integer, Promise> {

        @Override
        public void completed(Integer result, Promise attachment) {
          attachment.fulfil(fileTuple.copy(Unit.INSTANCE, fileTuple.position() + result), FileWriteFileNode.this);
        }

        @Override
        public void failed(Throwable exc, Promise attachment) {
          attachment.fulfil(new yatta.runtime.exceptions.IOException(exc.getMessage(), FileWriteFileNode.this), FileWriteFileNode.this);
        }
      }

      final Promise promise = new Promise(interopLibrary);

      try {
        ByteBuffer byteBuffer = data.asByteBuffer(this);
        fileTuple.fileHandle().write(byteBuffer, fileTuple.position(), promise, new WriteCompletionHandler());
      } catch (BufferOverflowException ex) {
        promise.fulfil(new yatta.runtime.exceptions.IOException(ex, this), this);
      } catch (Exception ex) {
        promise.fulfil(new YattaException(ex, this), this);
      }

      return promise;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileOpenNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileCloseNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.CreateTempFileNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileDeleteNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FilePathNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileSeekNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileListNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileReadLineNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileReadFileNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileWriteFileNodeFactory.getInstance()));
    return builtins;
  }
}
