package yatta.ast.builtin.modules;


import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleFile;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
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
import yatta.runtime.async.TransactionalMemory;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;
import yatta.runtime.stdlib.PrivateFunction;

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

  protected static final class FileContextManager extends ContextManager<FileTuple> {
    private final Context context;

    public FileContextManager(FileTuple fileTuple, Context context) {
      super("file", context.lookupGlobalFunction("File", "run"), fileTuple);
      this.context = context;
    }

    public FileContextManager copy(Object readBuffer, long position) {
      return new FileContextManager(new FileTuple(data().fileHandle(), readBuffer, position, data().additionalOptions(), data().path()), context);
    }

    public static FileContextManager adopt(ContextManager<?> contextManager, Context context) {
      return new FileContextManager((FileTuple) contextManager.data(), context);
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
        return new FileContextManager(new FileTuple(asynchronousFileChannel, Unit.INSTANCE, 0L, fileOptions.additionalOptions, Seq.fromCharSequence(path.toString())), context);
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
        case "delete_on_close":
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

  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(ContextManager contextManager, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YattaLanguage.class) Context context) {
      FileContextManager fileContextManager = FileContextManager.adopt(contextManager, context);
      boolean shouldClose = true;
      try {
        Object result = dispatch.execute(function);
        if (result instanceof Promise) {
          Promise resultPromise = (Promise) result;
          shouldClose = false;
          return resultPromise.map(value -> {
            try {
              return value;
            } finally {
              RunBuiltin.closeFile(fileContextManager, value, this, context);
            }
          }, exception -> RunBuiltin.closeFile(fileContextManager, exception, this, context), this);
        } else {
          return result;
        }
      } catch (UnsupportedTypeException | UnsupportedMessageException | ArityException e) {
        throw new YattaException(e, this);
      } finally {
        if (shouldClose) {
          RunBuiltin.closeFile(fileContextManager, Unit.INSTANCE, this, context);
        }
      }
    }

    private static <T> T closeFile(FileContextManager fileContextManager, T result, Node node, Context context) {
      try {
        AsynchronousFileChannel fileHandle = fileContextManager.data().fileHandle();
        fileHandle.close();
        if (fileContextManager.data().additionalOptions().contains(context.symbol("delete_on_close"), node)) {
          new File(fileContextManager.data().path().asJavaString(node)).delete();
        }
        return result;
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, node);
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

  @NodeInfo(shortName = "path")
  abstract static class FilePathNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Seq path(ContextManager contextManager, @CachedContext(YattaLanguage.class) Context context) {
      FileContextManager fileContextManager = FileContextManager.adopt(contextManager, context);
      return fileContextManager.data().path();
    }
  }

  @NodeInfo(shortName = "delete")
  abstract static class FileDeleteNode extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Unit path(ContextManager contextManager, @CachedContext(YattaLanguage.class) Context context) {
      FileContextManager fileContextManager = FileContextManager.adopt(contextManager, context);
      try {
        Files.deleteIfExists(Paths.get(fileContextManager.data().path().asJavaString(this)));
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
    public FileTuple open(ContextManager contextManager, long position, @CachedContext(YattaLanguage.class) Context context) {
      FileContextManager fileContextManager = FileContextManager.adopt(contextManager, context);
      return fileContextManager.data().seek(position);
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
    public Promise readline(ContextManager contextManager, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary interopLibrary) {
      FileContextManager fileContextManager = FileContextManager.adopt(contextManager, context);
      AsynchronousFileChannel asynchronousFileChannel = (AsynchronousFileChannel) ((NativeObject) fileContextManager.data().get(0)).getValue();
      ByteBuffer buffer = ByteBuffer.allocate(FILE_READ_BUFFER_SIZE);
      Node thisNode = this;
      Promise promise = new Promise(interopLibrary);

      try {
        asynchronousFileChannel.read(buffer, fileContextManager.data().position(), buffer, new CompletionHandler<>() {
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

            if (fileContextManager.data().readBuffer() instanceof ByteBuffer) {
              output = (ByteBuffer) fileContextManager.data().readBuffer();
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
                promise.fulfil(new Tuple(context.symbol("ok"), bytesToSeq(output, fileContextManager.data().additionalOptions(), context, thisNode), fileContextManager.copy(null, fileContextManager.data().position() + i + 1)), thisNode);
                fulfilled = true;
                break;
              } else {
                output.put(b);
              }
            }
            attachment.clear();

            if (!fulfilled) {
              readline(fileContextManager.copy(output, fileContextManager.data().position() + length), context, interopLibrary).map(res -> {
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
    public Promise readfile(ContextManager contextManager, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary interopLibrary) {
      FileContextManager fileContextManager = FileContextManager.adopt(contextManager, context);
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

            if (fileContextManager.data().additionalOptions().contains(context.symbol("binary"), FileReadFileNode.this)) {
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
        fileContextManager.data().fileHandle().read(buffer, fileContextManager.data().position(), buffer, new CatenateCompletionHandler(fileContextManager.data().fileHandle(), promise, fileContextManager.data().position()));
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
    public Promise writefile(ContextManager contextManager, Seq data, @CachedLibrary(limit = "3") InteropLibrary interopLibrary, @CachedContext(YattaLanguage.class) Context context) {
      FileContextManager fileContextManager = FileContextManager.adopt(contextManager, context);
      final class WriteCompletionHandler implements CompletionHandler<Integer, Promise> {

        @Override
        public void completed(Integer result, Promise attachment) {
          attachment.fulfil(fileContextManager.data().copy(Unit.INSTANCE, fileContextManager.data().position() + result), FileWriteFileNode.this);
        }

        @Override
        public void failed(Throwable exc, Promise attachment) {
          attachment.fulfil(new yatta.runtime.exceptions.IOException(exc.getMessage(), FileWriteFileNode.this), FileWriteFileNode.this);
        }
      }

      final Promise promise = new Promise(interopLibrary);

      try {
        ByteBuffer byteBuffer = data.asByteBuffer(this);
        fileContextManager.data().fileHandle().write(byteBuffer, fileContextManager.data().position(), promise, new WriteCompletionHandler());
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
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.CreateTempFileNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileDeleteNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FilePathNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileSeekNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileListNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileReadLineNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileReadFileNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.FileWriteFileNodeFactory.getInstance()));
    builtins.register(new ExportedFunction(FileBuiltinModuleFactory.RunBuiltinFactory.getInstance()));
    return builtins;
  }
}
