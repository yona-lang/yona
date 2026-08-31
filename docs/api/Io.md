# Std.Io

Std\Io — non-blocking console and raw-descriptor byte I/O.

Every operation that can block on a slow device submits through
io_uring (on Linux) or the thread pool (for reads), returns a
`Promise`, and auto-awaits at the use site. Synchronous calls:
`isTty`, `flush`, `readStdin` (stdin to EOF), and `readExact`
(pipe-safe exact-length read for LSP-style framing).

Trivial programs stay trivial: `println "hello"` is still one line.
The non-blocking machinery is invisible until you put several I/O
calls in a let block, at which point the structured-concurrency
grouping runs them concurrently.

```
import println, readLine from Std\Io in
do
println "What is your name?"
case readLine of
Some name -> println "Hello, {name}"
None      -> println "Goodbye."
end
end
```

## Functions

### `stdinFd : Int = 0`

File descriptor numbers, exposed as Int for raw-descriptor operations such
as console I/O, pipes, sockets, and LSP framing. Typed filesystem handles
use the separate operations in `Std\File`.

### `stdoutFd : Int = 1`

### `stderrFd : Int = 2`

### `print : String -> ()`

Write `s` to stdout. Returns a Promise that resolves when the kernel
has accepted the write. Non-blocking.

### `println : String -> ()`

Write `s` followed by a newline to stdout. Non-blocking.

### `eprint : String -> ()`

Write `s` to stderr. Non-blocking.

### `eprintln : String -> ()`

Write `s` followed by a newline to stderr. Non-blocking.

### `putStr : Int -> String -> ()`

Write `s` to an arbitrary fd. Non-blocking.

### `putStrLn : Int -> String -> ()`

Write `s` followed by a newline to an arbitrary fd. Non-blocking.

### `write : Int -> String -> ()`

`write fd s` is an alias for `putStr fd s`. Kept for when you want
the "I'm emitting bytes, not printing text" shape at the call site.

### `readLine : a`

Read one line from stdin, stripping trailing '\n' (and any '\r').
Returns `Some line` or `None` at EOF. Non-blocking — the read runs
on a thread-pool worker.

### `readLineFrom : Int -> a`

Read one line from an arbitrary fd. Same semantics as `readLine`.

### `readStdin : String`

Read stdin to EOF as a string. Used when the program source *is*
stdin (`yona -` or a pipe); a file script still inherits stdin.

### `readExact : Int -> Int -> Result (String, String)`

Read exactly `n` bytes from `fd` with stream `read` (not seek/`pread`).
Safe on pipes and sockets — required for LSP `Content-Length` framing
on stdin. `fd` is a raw descriptor such as `stdinFd`; use
`Std\File.readExact` for a typed `FileHandle`.
On Windows, a raw stdio fd (`0`/`1`/`2`) is switched to binary mode so
CRT text-mode CRLF translation cannot desync the frame.

```
readExact stdinFd 16
```

### `readExactBytes : Int -> Int -> String`

Stream `read` of up to `n` bytes (may be short at EOF). Pipe-safe.

### `flush : Int -> Bool`

`True` if `fd` is attached to a terminal (as opposed to a pipe or file).
Useful for turning off colored output or prompting prefixes.
Force pending writes on `fd` to disk / device (`fsync`). Most
io_uring writes are durable on completion so this is rarely needed.

### `isTty : Int -> Bool`

### `writeBytes : Int -> String -> ()`

Synchronous write (no Promise/await). Use for pipe-safe LSP framing.
On Windows, a raw stdio fd is switched to binary mode (same as
`readExact`) so `\r\n` headers stay byte-exact.
