# Std.Http

HTTP client and server — built on Std\Net and Std\String.

Provides Request/Response types, high-level client (get, post),
and a simple server (serve). All networking via io_uring.

## Types

### `type Method = GET | POST | PUT | DELETE | PATCH | HEAD | OPTIONS`

HTTP method.

### `type Request = Request {`

HTTP request with method, path, headers, and body.

### `type Response = Response {`

HTTP response with status code, raw headers string, and body.

## Functions

### `request`

```yona
request : Method -> String -> Int -> String -> Request
```

Create a request with defaults.

### `request`

```yona
request method path headers body = Request { method = method, path = path, headers = headers, body = body }
```

### `methodString`

```yona
methodString : Method -> String
```

Format a Method as a string.

### `methodString`

```yona
methodString m = case m of
```

### `formatRequest`

```yona
formatRequest : String -> Request -> String
```

Format a Request into an HTTP/1.1 request string.

### `formatRequest`

```yona
formatRequest host req =
```

### `parseResponse`

```yona
parseResponse : String -> Response
```

Parse an HTTP response string into a Response.

### `parseResponse`

```yona
parseResponse raw =
```

### `send`

```yona
send : String -> Int -> Request -> Response
```

Send an HTTP request to host:port and return the Response.

```
send "example.com" 80 (request GET "/" 0 "")
```

### `send`

```yona
send host port req =
```

### `get`

```yona
get : String -> Int -> String -> Response
```

HTTP GET shorthand.

```
get "example.com" 80 "/api"
```

### `get`

```yona
get host port path = send host port (Request { method = GET, path = path, headers = 0, body = "" })
```

### `post`

```yona
post : String -> Int -> String -> String -> Response
```

HTTP POST shorthand.

```
post "example.com" 80 "/submit" "key=value"
```

### `post`

```yona
post host port path body = send host port (Request { method = POST, path = path, headers = 0, body = body })
```

### `ok`

```yona
ok : String -> Response
```

Create a simple 200 OK response.

```
ok "Hello, World!"
```

### `ok`

```yona
ok body = Response { status = 200, rawHeaders = "", body = body }
```

### `notFound`

```yona
notFound = Response { status = 404, rawHeaders = "", body = "Not Found" }
```

Create a 404 Not Found response.

### `serverError`

```yona
serverError = Response { status = 500, rawHeaders = "", body = "Internal Server Error" }
```

Create a 500 Internal Server Error response.

### `formatResponse`

```yona
formatResponse : Response -> String
```

Format a Response into an HTTP/1.1 response string.

### `formatResponse`

```yona
formatResponse resp =
```

### `response`

```yona
response : Int -> String -> Response
```

Create a Response with custom status and body.

### `response`

```yona
response status body = Response { status = status, rawHeaders = "", body = body }
```

### `serveLoop`

```yona
serveLoop : Int -> (Request -> Response) -> Int
```

Start an HTTP server. Calls `handler request` for each incoming connection.
The handler receives a Request and returns a Response.
Runs forever (blocking).

```
serve "0.0.0.0" 8080 (\req -> ok "Hello!")
```

### `serveLoop`

```yona
serveLoop listener handler =
```

### `serve`

```yona
serve : String -> Int -> (Request -> Response) -> Int
```

### `serve`

```yona
serve host port handler =
```

