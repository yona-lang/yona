# Std.Net

Net -- TCP and UDP networking with async I/O.

Provides TCP client/server sockets and UDP datagrams. Async operations
(`tcpConnect`, `tcpAccept`, `send`, `recv`, `sendBytes`, `recvBytes`)
use io_uring on Linux for non-blocking I/O.

## Functions

### `tcpConnect : String -> Int -> Linear a`

Connect to a TCP server. Async (io_uring). Returns a socket descriptor.

```yona
import tcpConnect, send, recv, close from Std\Net in
let sock = tcpConnect "example.com" 80 in
do
  send sock "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n"
  let resp = recv sock 4096 in
  println resp
  close sock
end
```

### `tcpListen : String -> Int -> Linear a`

Create a TCP server socket bound to `host:port`. Returns a listener descriptor.

```yona
import tcpListen, tcpAccept, close from Std\Net in
let server = tcpListen "0.0.0.0" 8080 in
let client = tcpAccept server in
close client
```

### `tcpAccept : Int -> Linear a`

Accept an incoming TCP connection. Async (io_uring). Returns a client socket descriptor.

### `send : Int -> String -> Int`

Send a string over a socket. Async (io_uring). Returns the number of bytes sent.

### `recv : Int -> Int -> String`

Receive up to `maxBytes` bytes from a socket as a string. Async (io_uring).

### `sendBytes : Int -> ByteArray -> Int`

Send a byte buffer over a socket. Async (io_uring). Returns the number of bytes sent.

### `recvBytes : Int -> Int -> ByteArray`

Receive up to `maxBytes` from a socket as a byte buffer. Async (io_uring).

### `close : Int -> Int`

Close a socket descriptor. Returns 0 on success.

### `udpBind : String -> Int -> Linear a`

Create a UDP socket bound to `host:port`. Returns a socket descriptor.

```yona
import udpBind, udpRecv, close from Std\Net in
let sock = udpBind "0.0.0.0" 9000 in
let msg = udpRecv sock 1024 in
close sock
```

### `udpSendTo : Int -> String -> Int -> String -> Int`

Send a UDP datagram to `host:port`. Returns the number of bytes sent.

```yona
import udpBind, udpSendTo from Std\Net in
let sock = udpBind "0.0.0.0" 0 in
udpSendTo sock "127.0.0.1" 9000 "hello"
```

### `udpRecv : Int -> Int -> String`

Receive a UDP datagram of up to `maxBytes`. Returns the data as a string.

### `peerAddress : Int -> String`

Returns the remote address of a connected socket as a string.

```yona
import tcpConnect, peerAddress from Std\Net in
let sock = tcpConnect "example.com" 80 in
peerAddress sock   # => "93.184.216.34"
```
