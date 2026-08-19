# Std.Process

Process -- process management, environment, and command execution.

Provides environment variable access, command execution with output
capture, and subprocess management with stdin/stdout pipes. Async
functions (`exec`, `execStatus`, `readAll`, `wait`) block the
current fiber without blocking the OS thread.

## Functions

### getenv

`getenv : String -> String`

Get the value of an environment variable. Returns an empty string if not set.

```yona
import getenv from Std\Process in
getenv "HOME"   # => "/home/user"
```

### getcwd

`getcwd : String`

Returns the current working directory.

```yona
import getcwd from Std\Process in
getcwd   # => "/home/user/project"
```

### exit

`exit : Int -> Int`

Terminate the process with the given exit code.

```yona
import exit from Std\Process in
exit 0
```

### exec

`exec : String -> String`

Execute a shell command and return its stdout as a string. Async.

```yona
import exec from Std\Process in
let output = exec "ls -la" in
println output
```

### execStatus

`execStatus : String -> Int`

Execute a shell command and return its exit status code. Async.

```yona
import execStatus from Std\Process in
let code = execStatus "make build" in
println (show code)
```

### setenv

`setenv : String -> String -> Int`

Set an environment variable. Returns 0 on success.

```yona
import setenv from Std\Process in
setenv "MY_VAR" "hello"
```

### hostname

`hostname : String`

Returns the system hostname.

```yona
import hostname from Std\Process in
hostname   # => "myhost"
```

### spawn

`spawn : String -> Int`

Spawn a subprocess without waiting for it to finish. Returns a process handle (Int).

```yona
import spawn, wait from Std\Process in
let proc = spawn "sleep 5" in
let status = wait proc in
println (show status)
```

### readLine

`readLine : Int -> String`

Read a single line from the subprocess stdout.

```yona
import spawn, readLine from Std\Process in
let proc = spawn "echo hello" in
readLine proc   # => "hello"
```

### readAll

`readAll : Int -> String`

Read all remaining stdout from a subprocess as a string. Async.

### wait

`wait : Int -> Int`

Wait for a subprocess to exit and return its exit status. Async.

### kill

`kill : Int -> Int -> Int`

Send a signal to a subprocess. Returns 0 on success.

```yona
import spawn, kill from Std\Process in
let proc = spawn "sleep 100" in
kill proc 15   # SIGTERM
```

### writeStdin

`writeStdin : Int -> String -> Int`

Write a string to the subprocess stdin. Returns the number of bytes written.

### closeStdin

`closeStdin : Int -> Int`

Close the stdin pipe of a subprocess. Returns 0 on success.

### pid

`pid : Int -> Int`

Returns the OS process ID of a subprocess.

```yona
import spawn, pid from Std\Process in
let proc = spawn "sleep 10" in
pid proc   # => 12345
```
