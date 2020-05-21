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
import yatta.runtime.strings.StringUtil;

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
          new NativeObject(client), additionalOptions
      };
    }

    public HttpClient httpClient() {
      return (HttpClient) ((NativeObject) items[0]).getValue();
    }

    public Set additionalOptions() {
      return (Set) items[1];
    }
  }

  @NodeInfo(shortName = "session")
  abstract static class SessionBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public Object session(Dict params, @CachedContext(YattaLanguage.class) Context context) {
      if (params.size() == 0L) {
        return new HttpSessionTuple(HttpClient.newHttpClient(), Set.empty());
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
            return new HttpSessionTuple(builder.build(), finalAdditionalOptions);
          }, this);
        }
      }

      return new HttpSessionTuple(builder.build(), additionalOptions);
    }

    @CompilerDirectives.TruffleBoundary
    private HttpClient.Redirect extractRedirectPolicy(Object redirectPolicy) {
      if (redirectPolicy instanceof Symbol) {
        String redirectPolicyString = ((Symbol) redirectPolicy).asString();
        switch (redirectPolicyString) {
          case "always":
            return HttpClient.Redirect.ALWAYS;
          case "never":
            return HttpClient.Redirect.NEVER;
          case "normal":
            return HttpClient.Redirect.NORMAL;
          default:
            throw new BadArgException(redirectPolicyString, this);
        }
      } else {
        throw YattaException.typeError(this, redirectPolicy);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Object extractAuthenticator(Object authenticatorObj) {
      if (authenticatorObj instanceof Tuple) {
        Object authenticatorTupleObj = ((Tuple) authenticatorObj).unwrapPromises(this);
        if (authenticatorTupleObj instanceof Object[]) {
          Object[] authenticatorItems = (Object[]) authenticatorTupleObj;
          if (authenticatorItems.length != 3) {
            throw new BadArgException("Authenticator tuple must have 3 elements: " + Arrays.toString(authenticatorItems), this);
          } else {
            if (authenticatorItems[0] instanceof Symbol) {
              Symbol authenticationTypeSymbol = (Symbol) authenticatorItems[0];
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
      } else if (authenticatorObj instanceof Promise) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        Promise authenticatorObjPromise = (Promise) authenticatorObj;
        return authenticatorObjPromise.map(this::extractAuthenticator, this);
      } else {
        throw YattaException.typeError(this, authenticatorObj);
      }
    }
  }

  abstract static class SendBuiltin extends BuiltinNode {
    @CompilerDirectives.TruffleBoundary
    protected Promise sendRequest(HttpSessionTuple sessionTuple, RequestType requestType, Seq uri, Dict headers, Seq body, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      Object requestObj = buildRequest(requestType, uri, headers, body);
      if (requestObj instanceof HttpRequest) {
        return runRequest(sessionTuple, (HttpRequest) requestObj, context, dispatch);
      } else {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        return ((Promise) requestObj).map(request -> runRequest(sessionTuple, (HttpRequest) request, context, dispatch), this);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Promise runRequest(HttpSessionTuple sessionTuple, HttpRequest request, Context context, InteropLibrary dispatch) {
      Promise promise = new Promise(dispatch);
      context.ioExecutor.submit(() -> {
        try {
          HttpResponse<?> response = sessionTuple.httpClient().send(request, bodyHandlerForHttpSession(sessionTuple, context));
          promise.fulfil(responseToTuple(sessionTuple, response, context), this);
        } catch (Exception e) {
          promise.fulfil(e, this);
        }
      });

      return promise;
    }

    @CompilerDirectives.TruffleBoundary
    private HttpResponse.BodyHandler<?> bodyHandlerForHttpSession(HttpSessionTuple sessionTuple, Context context) {
      if (sessionTuple.additionalOptions().contains(context.symbol("binary"))) {
        return HttpResponse.BodyHandlers.ofByteArray();
      } else {
        return HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8);
      }
    }

    @CompilerDirectives.TruffleBoundary
    private Tuple responseToTuple(HttpSessionTuple sessionTuple, HttpResponse<?> response, Context context) {
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

    private Seq bodyForHttpSession(HttpSessionTuple sessionTuple, HttpResponse<?> response, Context context) {
      if (sessionTuple.additionalOptions().contains(context.symbol("binary"))) {
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
          StringUtil.yattaValueAsYattaString(k).asJavaString(this),
          StringUtil.yattaValueAsYattaString(v).asJavaString(this)
      ));

      return requestType.buildRequest(builder, body, this);
    }

    protected enum RequestType {
      GET, POST, PUT, DELETE;

      public HttpRequest buildRequest(HttpRequest.Builder builder, Seq body, Node node) {
        switch (this) {
          case GET:
            return builder.GET().build();
          case POST:
            return builder.POST(bodyPublisher(body, node)).build();
          case PUT:
            return builder.PUT(bodyPublisher(body, node)).build();
          case DELETE:
            return builder.DELETE().build();
          default:
            throw new BadArgException(this.name(), node);
        }
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
    public Promise get(HttpSessionTuple sessionTuple, Seq uri, Dict headers, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(sessionTuple, RequestType.GET, uri, headers, null, context, dispatch);
    }
  }

  @NodeInfo(shortName = "delete")
  abstract static class DeleteBuiltin extends SendBuiltin {
    @Specialization
    public Promise delete(HttpSessionTuple sessionTuple, Seq uri, Dict headers, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(sessionTuple, RequestType.DELETE, uri, headers, null, context, dispatch);
    }
  }

  @NodeInfo(shortName = "post")
  abstract static class PostBuiltin extends SendBuiltin {
    @Specialization
    public Promise post(HttpSessionTuple sessionTuple, Seq uri, Dict headers, Seq body, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(sessionTuple, RequestType.POST, uri, headers, body, context, dispatch);
    }
  }

  @NodeInfo(shortName = "put")
  abstract static class PutBuiltin extends SendBuiltin {
    @Specialization
    public Promise put(HttpSessionTuple sessionTuple, Seq uri, Dict headers, Seq body, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sendRequest(sessionTuple, RequestType.PUT, uri, headers, body, context, dispatch);
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(HttpClientBuiltinModuleFactory.SessionBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(HttpClientBuiltinModuleFactory.GetBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(HttpClientBuiltinModuleFactory.DeleteBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(HttpClientBuiltinModuleFactory.PostBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(HttpClientBuiltinModuleFactory.PutBuiltinFactory.getInstance()));
    return builtins;
  }
}
