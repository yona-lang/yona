# Std.Http

HTTP client and server — built on Std\Net and Std\String.

Provides Request/Response types, high-level client (get, post),
and a simple server (serve). All networking via io_uring.

## Types

### Method

`type Method = GET | POST | PUT | DELETE | PATCH | HEAD | OPTIONS`

HTTP method.

### Request

```yona
type Request = Request {
    method : Method,
    path : String,
    headers : Int,
    body : String
}
```

HTTP request with method, path, headers, and body.

### Response

```yona
type Response = Response {
    status : Int,
    rawHeaders : String,
    body : String
}
```

HTTP response with status code, raw headers string, and body.

## Functions

### get

`get : String -> Int -> String -> Response`

HTTP GET shorthand.

```
get "example.com" 80 "/api"
```

### post

`post : String -> Int -> String -> String -> Response`

HTTP POST shorthand.

```
post "example.com" 80 "/submit" "key=value"
```

### request

`request : Method -> String -> Int -> String -> Request`

Create a request with defaults.

### send

`send : String -> Int -> Request -> Response`

Send an HTTP request to host:port and return the Response.

```
send "example.com" 80 (request GET "/" 0 "")
```

### parseResponse

`parseResponse : String -> Response`

Parse an HTTP response string into a Response.

### formatRequest

`formatRequest : String -> Request -> String`

Format a Request into an HTTP/1.1 request string.

### serve

`serve : String -> Int -> (Request -> Response) -> Int`

Start an HTTP server. Calls `handler request` for each incoming connection.
The handler receives a Request and returns a Response.
Runs forever (blocking).

```
serve "0.0.0.0" 8080 (\req -> ok "Hello!")
```

### response

`response : Int -> String -> Response`

Create a Response with custom status and body.

### ok

`ok : String -> Response`

Create a simple 200 OK response.

```
ok "Hello, World!"
```

### notFound

`notFound : Response`

Create a 404 Not Found response.

### serverError

`serverError : Response`

Create a 500 Internal Server Error response.
