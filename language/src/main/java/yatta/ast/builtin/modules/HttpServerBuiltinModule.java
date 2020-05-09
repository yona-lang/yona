package yatta.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.sun.net.httpserver.Headers;
import com.sun.net.httpserver.HttpContext;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import yatta.TypesGen;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.*;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.exceptions.util.ExceptionUtil;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;
import yatta.runtime.stdlib.util.TimeUnitUtil;
import yatta.runtime.strings.StringUtil;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.util.List;
import java.util.Map;

@BuiltinModuleInfo(packageParts = {"http"}, moduleName = "Server")
public final class HttpServerBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "create")
  abstract static class CreateBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public NativeObject create(Seq host, long port, long backlog, @CachedContext(YattaLanguage.class) Context context) {
      try {
        if (port > Integer.MAX_VALUE) {
          throw new BadArgException("Port must be < Integer.MAX_VALUE", this);
        }
        if (backlog > Integer.MAX_VALUE) {
          throw new BadArgException("Backlog must be < Integer.MAX_VALUE", this);
        }
        HttpServer server = HttpServer.create(new InetSocketAddress(host.asJavaString(this), (int) port), (int) backlog);
        server.setExecutor(context.ioExecutor);
        return new NativeObject(server);
      } catch (IOException e) {
        throw new yatta.runtime.exceptions.IOException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "start")
  abstract static class StartBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object start(NativeObject server) {
      Object hopefullyHttpServer = (server).getValue();
      if (hopefullyHttpServer instanceof HttpServer) {
        ((HttpServer) hopefullyHttpServer).start();
        return server;
      } else {
        throw YattaException.typeError(this, hopefullyHttpServer);
      }
    }
  }

  @NodeInfo(shortName = "stop")
  abstract static class StopBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object stop(NativeObject server, Tuple timeUnit) {
      Object delayObj = TimeUnitUtil.getSeconds(timeUnit, this);

      if (delayObj instanceof Long) {
        return stopServer(server, delayObj);
      } else { // Promise
        Promise delayPromise = (Promise) delayObj;
        return delayPromise.map(delay -> {
          try {
            return stopServer(server, delay);
          } catch (Exception e) {
            return e;
          }
        }, this);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Object stopServer(NativeObject server, Object delayObj) {
      try {
        long delay = TypesGen.expectLong(delayObj);
        if (delay > Integer.MAX_VALUE) {
          throw new BadArgException("Delay must be < " + Integer.MAX_VALUE, this);
        }
        Object hopefullyHttpServer = (server).getValue();
        if (hopefullyHttpServer instanceof HttpServer) {
          ((HttpServer) hopefullyHttpServer).stop((int) delay);
          return server;
        } else {
          throw YattaException.typeError(this, hopefullyHttpServer);
        }
      } catch (UnexpectedResultException e) {
        throw new BadArgException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "handle")
  abstract static class HandleBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object handle(Seq path, Function handler, NativeObject server, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Object hopefullyHttpServer = (server).getValue();
      if (hopefullyHttpServer instanceof HttpServer) {
        HttpServer httpServer = (HttpServer) server.getValue();
        HttpContext httpContext = httpServer.createContext(path.asJavaString(this), (httpExchange) -> {
          Dict exchangeParams = Dict.empty()
              .add("local_address", Seq.fromCharSequence(httpExchange.getLocalAddress().toString()))
              .add("protocol", Seq.fromCharSequence(httpExchange.getProtocol()))
              .add("remote_address", Seq.fromCharSequence(httpExchange.getRemoteAddress().toString()))
              .add("method", context.symbol(httpExchange.getRequestMethod()))
              .add("uri", Seq.fromCharSequence(httpExchange.getRequestURI().toString()));
          Dict headers = headersToDict(httpExchange.getRequestHeaders());
          byte[] bodyBytes = httpExchange.getRequestBody().readAllBytes();
          Seq body = Seq.fromBytes(bodyBytes);
          try {
            Object handlerResult = dispatch.execute(handler, exchangeParams, headers, body);
            sendResponse(handlerResult, httpExchange);
          } catch (Throwable e) {
            StringBuilder errorMsg = new StringBuilder();
            errorMsg.append("Internal Server Error: ");
            appendExceptionStackTrace(ExceptionUtil.throwableToTuple(e, context), errorMsg);
            String errorMsgStr = errorMsg.toString();
            httpExchange.sendResponseHeaders(500, errorMsgStr.length());
            httpExchange.getResponseBody().write(errorMsgStr.getBytes());
          }
        });
        return server;
      } else {
        throw YattaException.typeError(this, hopefullyHttpServer);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private void appendExceptionStackTrace(Tuple tuple, StringBuilder errorMsg) {
      errorMsg.append("(");
      errorMsg.append(tuple.get(0).toString());
      errorMsg.append("): ");
      errorMsg.append(tuple.get(1).toString());
      errorMsg.append("\r\n");
      Seq stacktrace = (Seq) tuple.get(2);
      stacktrace.foldLeft(errorMsg, (acc, el) -> {
        acc.append(el.toString());
        acc.append("\r\n");
        return acc;
      });
    }

    @CompilerDirectives.TruffleBoundary
    private Object sendResponse(Object result, HttpExchange httpExchange) {
      if (result instanceof Tuple) {
        Tuple resultTuple = (Tuple) result;
        if (resultTuple.length() == 3) {
          Object unwrappedResultTuple = resultTuple.unwrapPromises(this);
          if (unwrappedResultTuple instanceof Object[]) {
            Object[] elements = (Object[]) unwrappedResultTuple;
            if (elements[0] instanceof Long && elements[1] instanceof Dict && elements[2] instanceof Seq) {
              long rCode = (long) elements[0];
              if (rCode > Integer.MAX_VALUE) {
                throw new BadArgException("Invalid response code, it must be < Integer.MAX_VALUE: " + rCode, this);
              }
              Dict headers = (Dict) elements[1];
              Seq body = (Seq) elements[2];
              try {
                writeResponseHeaders(headers, httpExchange.getResponseHeaders());
                httpExchange.sendResponseHeaders((int) rCode, body.length());
                httpExchange.getResponseBody().write(body.asByteArray(this));
                httpExchange.close();
              } catch (IOException e) {
                throw new yatta.runtime.exceptions.IOException(e, this);
              }
            }
          } else { // Promise
            CompilerDirectives.transferToInterpreterAndInvalidate();
            Promise unwrappedResultTuplePromise = (Promise) unwrappedResultTuple;
            return unwrappedResultTuplePromise.map(res -> sendResponse(new Tuple(res), httpExchange), this);
          }
          return Unit.INSTANCE;
        }
      } else if (result instanceof Promise) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        Promise resultPromise = (Promise) result;
        return resultPromise.map(res -> sendResponse(res, httpExchange), this);
      }

      throw new BadArgException("Invalid return value of an HTTP handler. It must return a triple (status_code, headers, body).", this);
    }

    @CompilerDirectives.TruffleBoundary
    private void writeResponseHeaders(Dict headersDict, Headers headers) {
      headersDict.forEach((k, v) -> {
        headers.add(
            StringUtil.yattaValueAsYattaString(k).asJavaString(this),
            StringUtil.yattaValueAsYattaString(v).asJavaString(this)
        );
      });
    }

    @CompilerDirectives.TruffleBoundary
    private Dict headersToDict(Headers headers) {
      Dict headersDict = Dict.empty();
      for (Map.Entry<String, List<String>> entry : headers.entrySet()) {
        Seq value = Seq.EMPTY;
        for (String val : entry.getValue()) {
          value = value.insertLast(Seq.fromCharSequence(val));
        }
        headersDict = headersDict.add(Seq.fromCharSequence(entry.getKey()), value);
      }
      return headersDict;
    }
  }

  @Override
  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(HttpServerBuiltinModuleFactory.CreateBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(HttpServerBuiltinModuleFactory.StartBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(HttpServerBuiltinModuleFactory.StopBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(HttpServerBuiltinModuleFactory.HandleBuiltinFactory.getInstance()));
    return builtins;
  }
}
