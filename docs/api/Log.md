# Std.Log

Log -- leveled logging to stderr.

Provides debug, info, warn, and error log levels with a global
level filter. Messages below the current level are suppressed.

## Functions

### debug

`debug : String -> ()`

Log a message at DEBUG level.

```yona
import debug from Std\Log in
debug "entering function foo"
```

### info

`info : String -> ()`

Log a message at INFO level.

```yona
import info from Std\Log in
info "server started on port 8080"
```

### warn

`warn : String -> ()`

Log a message at WARN level.

```yona
import warn from Std\Log in
warn "disk usage above 90%"
```

### error

`error : String -> ()`

Log a message at ERROR level.

```yona
import error from Std\Log in
error "failed to connect to database"
```

### setLevel

`setLevel : Int -> ()`

Set the global log level. Use 0 = DEBUG, 1 = INFO, 2 = WARN, 3 = ERROR.

```yona
import setLevel from Std\Log in
setLevel 2   # only WARN and ERROR messages will appear
```

### getLevel

`getLevel : Int`

Returns the current global log level as an integer.

```yona
import getLevel from Std\Log in
getLevel   # => 1 (INFO by default)
```
