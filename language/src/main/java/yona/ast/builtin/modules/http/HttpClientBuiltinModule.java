package yona.ast.builtin.modules.http;

import com.oracle.truffle.api.CompilerDirectives;
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
import yona.TypesGen;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.ast.builtin.modules.BuiltinModule;
import yona.ast.builtin.modules.BuiltinModuleInfo;
import yona.ast.builtin.modules.socket.ConnectionContextManager;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.network.TCPConnection;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;
import yona.runtime.strings.StringUtil;

import java.net.Authenticator;
import java.net.PasswordAuthentication;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

@BuiltinModuleInfo(packageParts = {"http"}, moduleName = "Client")
public final class HttpClientBuiltinModule implements BuiltinModule {
  protected static final class HttpSessionTuple extends Tuple {
    public HttpSessionTuple(HttpClient client, Set additionalOptions) {
      this.items = new Object[]{
        new NativeObject<>(client), additionalOptions
      };
    }

    public HttpClient httpClient(Node node) {
      return ((NativeObject<HttpClient>) items[0]).getValue();
    }

    public Set additionalOptions() {
      return (Set) items[1];
    }
  }

  protected static final class HttpSessionConnectionManager extends NativeObjectContextManager<HttpSessionTuple> {
    public HttpSessionConnectionManager(HttpClient httpClient, Set additionalOptions, Context context) {
      super("http_session", context.lookupGlobalFunction("http\\Client", "run"), new HttpSessionTuple(httpClient, additionalOptions));
    }

    public HttpSessionConnectionManager(HttpSessionTuple data, Context context) {
      super("http_session", context.lookupGlobalFunction("http\\Client", "run"), data);
    }

    public static HttpSessionConnectionManager adapt(ContextManager<?> contextManager, Context context, Node node) {
      return new HttpSessionConnectionManager(((NativeObject<HttpSessionTuple>) contextManager.getData(NativeObject.class, node)).getValue(), context);
    }
  }

  @NodeInfo(shortName = "run")
  abstract static class RunBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object run(ContextManager<?> contextManager, Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch, @CachedContext(YonaLanguage.class) Context context) {
      HttpSessionConnectionManager httpClientContextManager = HttpSessionConnectionManager.adapt(contextManager, context, this);
      try {
        return dispatch.execute(function);
      } catch (UnsupportedTypeException | UnsupportedMessageException | ArityException e) {
        throw new YonaException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "session")
  abstract static class SessionBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object session(Dict params, @CachedContext(YonaLanguage.class) Context context) {
      if (params.size() == 0L) {
        return new HttpSessionConnectionManager(HttpClient.newHttpClient(), Set.empty(), context);
      } else {
        HttpClient.Builder builder = HttpClient.newBuilder().executor(context.ioExecutor);

        Object unwrappedParams = params.unwrapPromises(this);
        if (unwrappedParams instanceof Dict) {
          return buildSession((Dict) unwrappedParams, context, builder);
        } else { // Promise
          CompilerDirectives.transferToInterpreterAndInvalidate();
          Promise paramsPromise = (Promise) unwrappedParams;
          return paramsPromise.map((paramsDict) -> buildSession((Dict) paramsDict, context, builder), this);
        }
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Object buildSession(Dict params, Context context, HttpClient.Builder builder) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      Set additionalOptions = Set.empty();
      Symbol followRedirectsSymbol = context.symbol("follow_redirects");
      if (params.contains(followRedirectsSymbol)) {
        builder.followRedirects(extractRedirectPolicy(params.lookup(followRedirectsSymbol)));
      }

      Symbol bodyEncodingSymbol = context.symbol("body_encoding");
      if (params.contains(bodyEncodingSymbol)) {
        Object bodyEncoding = params.lookup(bodyEncodingSymbol);
        if (bodyEncoding instanceof Symbol) {
          String bodyEncodingString = ((Symbol) bodyEncoding).asString();
          if ("binary".equals(bodyEncodingString) || "text".equals(bodyEncodingString)) {
            additionalOptions = additionalOptions.add(bodyEncoding);
          } else {
            throw new BadArgException("Accepted values for body_encoding are :binary or :text. Text is always UTF-8. Text is the default choice.", this);
          }
        }
      }

      Symbol authenticatorSymbol = context.symbol("authenticator");
      if (params.contains(authenticatorSymbol)) {
        Object authenticatorObj = extractAuthenticator(params.lookup(authenticatorSymbol));
        if (authenticatorObj instanceof Authenticator) {
          builder.authenticator((Authenticator) authenticatorObj);
        } else { // Promise
          CompilerDirectives.transferToInterpreterAndInvalidate();
          Promise authenticatorPromise = (Promise) authenticatorObj;
          Set finalAdditionalOptions = additionalOptions;
          return authenticatorPromise.map(authenticator -> {
            CompilerDirectives.transferToInterpreterAndInvalidate();
            builder.authenticator((Authenticator) authenticator);
            return new HttpSessionConnectionManager(builder.build(), finalAdditionalOptions, context);
          }, this);
        }
      }

      return new HttpSessionConnectionManager(builder.build(), additionalOptions, context);
    }

    @CompilerDirectives.TruffleBoundary
    private HttpClient.Redirect extractRedirectPolicy(Object redirectPolicy) {
      if (redirectPolicy instanceof Symbol) {
        String redirectPolicyString = ((Symbol) redirectPolicy).asString();
        return switch (redirectPolicyString) {
          case "always" -> HttpClient.Redirect.ALWAYS;
          case "never" -> HttpClient.Redirect.NEVER;
          case "normal" -> HttpClient.Redirect.NORMAL;
          default -> throw new BadArgException(redirectPolicyString, this);
        };
      } else {
        throw YonaException.typeError(this, redirectPolicy);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Object extractAuthenticator(Object authenticatorObj) {
      if (authenticatorObj instanceof Tuple) {
        Object authenticatorTupleObj = ((Tuple) authenticatorObj).unwrapPromises(this);
        if (authenticatorTupleObj instanceof Object[] authenticatorItems) {
          if (authenticatorItems.length != 3) {
            throw new BadArgException("Authenticator tuple must have 3 elements: " + Arrays.toString(authenticatorItems), this);
          } else {
            if (authenticatorItems[0] instanceof Symbol authenticationTypeSymbol) {
              if ("password".equals(authenticationTypeSymbol.asString())) {
                try {
                  Seq username = TypesGen.expectSeq(authenticatorItems[1]);
                  Seq password = TypesGen.expectSeq(authenticatorItems[2]);
                  final Node node = this;

                  return new Authenticator() {
                    @Override
                    protected PasswordAuthentication getPasswordAuthentication() {
                      return new PasswordAuthentication(username.asJavaString(node), password.asJavaString(node).toCharArray());
                    }
                  };
                } catch (UnexpectedResultException e) {
                  throw new BadArgException(e, this);
                }
              } else {
                throw new BadArgException("Supported authenticator types are: password", this);
              }
            } else {
              throw new BadArgException("First element in the authenticator tuple must be a symbol, not: " + authenticatorItems[0], this);
            }
          }
        } else { // Promise
          CompilerDirectives.transferToInterpreterAndInvalidate();
          Promise authenticatorTuplePromise = (Promise) authenticatorTupleObj;
          return authenticatorTuplePromise.map(this::extractAuthenticator, this);
        }
      } else if (authenticatorObj instanceof Promise authenticatorObjPromise) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        return authenticatorObjPromise.map(this::extractAuthenticator, this);
      } else {
        throw YonaException.typeError(this, authenticatorObj);
      }
    }
  }

  abstract static class SendBuiltin extends BuiltinNode {
    @CompilerDirectives.TruffleBoundary
    protected Promise sendRequest(ContextManager<?> contextManager, RequestType requestType, Seq uri, Dict headers, Seq body, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      HttpSessionConnectionManager httpSessionConnectionManager = HttpSessionConnectionManager.adapt(contextManager, context, this);
      Object requestObj = buildRequest(requestType, uri, headers, body);
      if (requestObj instanceof HttpRequest) {
        return runRequest(httpSessionConnectionManager, (HttpRequest) requestObj, context, dispatch);
      } else {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        return ((Promise) requestObj).map(request -> runRequest(httpSessionConnectionManager, (HttpRequest) request, context, dispatch), this);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Promise runRequest(HttpSessionConnectionManager sessionTuple, HttpRequest request, Context context, InteropLibrary dispatch) {
      Promise promise = new Promise(dispatch);
      context.ioExecutor.submit(() -> {
        try {
          HttpResponse<?> response = sessionTuple.nativeData(this).httpClient(this).send(request, bodyHandlerForHttpSession(sessionTuple, context));
          promise.fulfil(responseToTuple(sessionTuple, response, context), this);
        } catch (Exception e) {
          promise.fulfil(e, this);
        }
      });

      return promise;
    }

    @CompilerDirectives.TruffleBoundary
    private HttpResponse.BodyHandler<?> bodyHandlerForHttpSession(HttpSessionConnectionManager sessionTuple, Context context) {
      if (sessionTuple.nativeData(this).additionalOptions().contains(context.symbol("binary"))) {
        return HttpResponse.BodyHandlers.ofByteArray();
      } else {
        return HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Tuple responseToTuple(HttpSessionConnectionManager sessionTuple, HttpResponse<?> response, Context context) {
      Dict headers = Dict.EMPTY;
      for (Map.Entry<String, List<String>> entry : response.headers().map().entrySet()) {
        Seq value = Seq.EMPTY;
        for (String val : entry.getValue()) {
          value = value.insertLast(Seq.fromCharSequence(val));
        }
        headers = headers.add(Seq.fromCharSequence(entry.getKey()), value);
      }
      return new Tuple((long) response.statusCode(), headers, bodyForHttpSession(sessionTuple, response, context));
    }

    private Seq bodyForHttpSession(HttpSessionConnectionManager sessionTuple, HttpResponse<?> response, Context context) {
      if (sessionTuple.nativeData(this).additionalOptions().contains(context.symbol("binary"))) {
        return Seq.fromBytes((byte[]) response.body());
      } else {
        return Seq.fromCharSequence((String) response.body());
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Object buildRequest(RequestType requestType, Seq uri, Dict headers, Seq body) {
      HttpRequest.Builder builder = HttpRequest.newBuilder(URI.create(uri.asJavaString(this)));

      Object unwrappedHeadersObj = headers.unwrapPromises(this);
      if (unwrappedHeadersObj instanceof Promise) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        return ((Promise) unwrappedHeadersObj).map(unwrappedHeaders -> applyHeadersToRequest(requestType, body, builder, (Dict) unwrappedHeaders), this);
      } else {
        return applyHeadersToRequest(requestType, body, builder, (Dict) unwrappedHeadersObj);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private HttpRequest applyHeadersToRequest(RequestType requestType, Seq body, HttpRequest.Builder builder, Dict headers) {
      headers.forEach((k, v) -> builder.setHeader(
        StringUtil.yonaValueAsYonaString(k).asJavaString(this),
        StringUtil.yonaValueAsYonaString(v).asJavaString(this)
      ));

      return requestType.buildRequest(builder, body, this);
    }

    protected enum RequestType {
      GET, POST, PUT, DELETE;

      public HttpRequest buildRequest(HttpRequest.Builder builder, Seq body, Node node) {
        return switch (this) {
          case GET -> builder.GET().build();
          case POST -> builder.POST(bodyPublisher(body, node)).build();
          case PUT -> builder.PUT(bodyPublisher(body, node)).build();
          case DELETE -> builder.DELETE().build();
          default -> throw new BadArgException(this.name(), node);
        };
      }

      @CompilerDirectives.TruffleBoundary
      private HttpRequest.BodyPublisher bodyPublisher(Seq body, Node node) {
        return HttpRequest.BodyPublishers.ofByteArray(body.asByteArray(node));
      }
    }
  }

  @NodeInfo(shortName = "get")
  abstract static class GetBuiltin extends SendBuiltin {
    @Specialization
    public Promise get(ContextManager<?> contextManager, Seq uri, Dict headers, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(contextManager, RequestType.GET, uri, headers, null, context, dispatch);
    }
  }

  @NodeInfo(shortName = "delete")
  abstract static class DeleteBuiltin extends SendBuiltin {
    @Specialization
    public Promise delete(ContextManager<?> contextManager, Seq uri, Dict headers, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(contextManager, RequestType.DELETE, uri, headers, null, context, dispatch);
    }
  }

  @NodeInfo(shortName = "post")
  abstract static class PostBuiltin extends SendBuiltin {
    @Specialization
    public Promise post(ContextManager<?> contextManager, Seq uri, Dict headers, Seq body, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(contextManager, RequestType.POST, uri, headers, body, context, dispatch);
    }
  }

  @NodeInfo(shortName = "put")
  abstract static class PutBuiltin extends SendBuiltin {
    @Specialization
    public Promise put(ContextManager<?> contextManager, Seq uri, Dict headers, Seq body, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(contextManager, RequestType.PUT, uri, headers, body, context, dispatch);
    }
  }

  public Builtins builtins() {
    return new Builtins(
      new ExportedFunction(HttpClientBuiltinModuleFactory.SessionBuiltinFactory.getInstance()),
      new ExportedFunction(HttpClientBuiltinModuleFactory.RunBuiltinFactory.getInstance()),
      new ExportedFunction(HttpClientBuiltinModuleFactory.GetBuiltinFactory.getInstance()),
      new ExportedFunction(HttpClientBuiltinModuleFactory.DeleteBuiltinFactory.getInstance()),
      new ExportedFunction(HttpClientBuiltinModuleFactory.PostBuiltinFactory.getInstance()),
      new ExportedFunction(HttpClientBuiltinModuleFactory.PutBuiltinFactory.getInstance())
    );
  }
}
