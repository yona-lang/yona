# Std.IO

Std\IO — non-blocking console and handle-based byte I/O.

Every operation that can block on a slow device submits through
io_uring (on Linux) or the thread pool (for reads), returns a
`Promise`, and auto-awaits at the use site. The only synchronous
calls are pure syscalls that never block — `isTty`, `flush`.

Trivial programs stay trivial: `println "hello"` is still one line.
The non-blocking machinery is invisible until you put several I/O
calls in a let block, at which point the structured-concurrency
grouping runs them concurrently.

```
import println, readLine from Std\IO in
let _ = println "What is your name?" in
case readLine of
Some name -> println "Hello, {name}"
None      -> println "Goodbye."
end
```

## Functions

### `stdinFd`

```yona
stdinFd  = 0
```

File descriptor numbers, exposed as Int so any Std\File handle call
that expects `FileHandle` can be wrapped — `FileHandle stdoutFd`
builds the Linear-compatible handle — and the raw int is also useful
for passing to `write` without constructing the ADT.

### `stdoutFd`

```yona
stdoutFd = 1
```

### `stderrFd`

```yona
stderrFd = 2
```

### `extern`

```yona
extern io    yona_Std_IO__writeStr    : Int -> String -> ()     = "yona_Std_IO__writeStr"
```

----- Low-level externs ----------------------------------------------------

The Yona names are kept short and friendly; the actual C symbols live
in `compiled_runtime.c` and submit to `io_uring` via the platform layer.
All four write primitives are non-blocking (`IO` in .yonai terms) and
return a Promise that auto-awaits at the call site. `readLineFd` is
`AFN` — thread-pool async — since line-buffering an io_uring read
stream is a v2 concern (see `docs/todo-list.md`).

### `extern`

```yona
extern io    yona_Std_IO__writeLine   : Int -> String -> ()     = "yona_Std_IO__writeLine"
```

### `extern`

```yona
extern async yona_Std_IO__readLineFd  : Int -> Option           = "yona_Std_IO__readLineFd"
```

### `extern`

```yona
extern       yona_Std_IO__isTty       : Int -> Bool             = "yona_Std_IO__isTty"
```

### `extern`

```yona
extern       yona_Std_IO__flushFd     : Int -> Bool             = "yona_Std_IO__flushFd"
```

### `print`

```yona
print s = yona_Std_IO__writeStr stdoutFd s
```

----- Output ---------------------------------------------------------------
Write `s` to stdout. Returns a Promise that resolves when the kernel
has accepted the write. Non-blocking.

### `println`

```yona
println s = yona_Std_IO__writeLine stdoutFd s
```

Write `s` followed by a newline to stdout. Non-blocking.

### `eprint`

```yona
eprint s = yona_Std_IO__writeStr stderrFd s
```

Write `s` to stderr. Non-blocking.

### `eprintln`

```yona
eprintln s = yona_Std_IO__writeLine stderrFd s
```

Write `s` followed by a newline to stderr. Non-blocking.

### `putStr`

```yona
putStr fd s = yona_Std_IO__writeStr fd s
```

Write `s` to an arbitrary fd. Non-blocking.

### `putStrLn`

```yona
putStrLn fd s = yona_Std_IO__writeLine fd s
```

Write `s` followed by a newline to an arbitrary fd. Non-blocking.

### `write`

```yona
write fd s = yona_Std_IO__writeStr fd s
```

`write fd s` is an alias for `putStr fd s`. Kept for when you want
the "I'm emitting bytes, not printing text" shape at the call site.

### `readLine`

```yona
readLine = yona_Std_IO__readLineFd stdinFd
```

----- Input ----------------------------------------------------------------
Read one line from stdin, stripping trailing '\n' (and any '\r').
Returns `Some line` or `None` at EOF. Non-blocking — the read runs
on a thread-pool worker.

### `readLineFrom`

```yona
readLineFrom fd = yona_Std_IO__readLineFd fd
```

Read one line from an arbitrary fd. Same semantics as `readLine`.

### `isTty`

```yona
isTty fd = yona_Std_IO__isTty fd
```

----- Handle control -------------------------------------------------------
`True` if `fd` is attached to a terminal (as opposed to a pipe or file).
Useful for turning off colored output or prompting prefixes.

### `isatty`

```yona
isatty fd = yona_Std_IO__isTty fd
```

Alias for `isTty`, following the libc spelling.

### `flush`

```yona
flush fd = yona_Std_IO__flushFd fd
```

Force pending writes on `fd` to disk / device (`fsync`). Most
io_uring writes are durable on completion so this is rarely needed.

