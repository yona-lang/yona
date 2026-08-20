# Std.Process

Process -- process management, environment, and command execution.

Provides environment variable access, command execution with output
capture, and subprocess management with stdin/stdout pipes. Async
functions (`exec`, `execStatus`, `readAll`, `wait`) block the
current fiber without blocking the OS thread.

## Functions

### `getenv : String -> String`

Get the value of an environment variable. Returns an empty string if not set.

```yona
import getenv from Std\Process in
getenv "HOME"   # => "/home/user"
```

### `getcwd : String`

Returns the current working directory.

```yona
import getcwd from Std\Process in
getcwd   # => "/home/user/project"
```

### `exit : Int -> Int`

Terminate the process with the given exit code.

```yona
import exit from Std\Process in
exit 0
```

### `exec : String -> String`

Execute a shell command and return its stdout as a string. Async.

```yona
import exec from Std\Process in
let output = exec "ls -la" in
println output
```

### `execStatus : String -> Int`

Execute a shell command and return its exit status code. Async.

```yona
import execStatus from Std\Process in
let code = execStatus "make build" in
println (show code)
```

### `setenv : String -> String -> Int`

Set an environment variable. Returns 0 on success.

```yona
import setenv from Std\Process in
setenv "MY_VAR" "hello"
```

### `hostname : String`

Returns the system hostname.

```yona
import hostname from Std\Process in
hostname   # => "myhost"
```

### `spawn : String -> Linear a`

Spawn a subprocess without waiting for it to finish. Returns a process handle (Int).

```yona
import spawn, wait from Std\Process in
let proc = spawn "sleep 5" in
let status = wait proc in
println (show status)
```

### `readLine : Int -> String`

Read a single line from the subprocess stdout.

```yona
import spawn, readLine from Std\Process in
let proc = spawn "echo hello" in
readLine proc   # => "hello"
```

### `readAll : Int -> String`

Read all remaining stdout from a subprocess as a string. Async.

### `wait : Int -> Int`

Wait for a subprocess to exit and return its exit status. Async.

### `kill : Int -> Int -> Int`

Send a signal to a subprocess. Returns 0 on success.

```yona
import spawn, kill from Std\Process in
let proc = spawn "sleep 100" in
kill proc 15   # SIGTERM
```

### `writeStdin : Int -> String -> Int`

Write a string to the subprocess stdin. Returns the number of bytes written.

### `closeStdin : Int -> Int`

Close the stdin pipe of a subprocess. Returns 0 on success.

### `pid : Int -> Int`

Returns the OS process ID of a subprocess.

```yona
import spawn, pid from Std\Process in
let proc = spawn "sleep 10" in
pid proc   # => 12345
```

### `getArgs : [a]`

POSIX `argv` as a sequence of strings: program or script path, then user
arguments. After `yonac -o hello && ./hello a b`, the first element is
`./hello`. After `yona script.yona a b`, the first element is the script
path.

### `executablePath : String`

Absolute path of the running executable (`/proc/self/exe` on Linux).

### `yonaVersion : String`

The same version string as `yonac --version`.

### `tempDir : String`

Directory for temporary files (`TMPDIR` / `TEMP` / platform default).

### `tempFile : String -> String -> String`

Create a unique empty file from a prefix and suffix (`mkstemp` style).
The prefix must not contain path separators.

### `run : String -> [a] -> Int`

Execute `file` with a full argv vector (no shell). Inherit stdin, stdout,
and stderr. Wait and return the exit status.

### `execArgs : String -> [a] -> Int`

Replace the current process with `file` and the given argv (Unix `execve`;
Windows waits then exits).
