# Std.Time

Time -- timestamps, sleeping, and elapsed time measurement.

Provides monotonic and wall-clock timestamps in milliseconds
and microseconds, sleep, and formatted time output.

## Functions

### `now : Int`

Returns the current wall-clock time in milliseconds since the Unix epoch.

```yona
import now from Std\Time in
now   # => 1712678400000
```

### `nowMicros : Int`

Returns the current time in microseconds since the Unix epoch.

```yona
import nowMicros from Std\Time in
nowMicros   # => 1712678400000000
```

### `epoch : Int`

Returns the Unix epoch (0) as a timestamp. Useful as a base for relative calculations.

```yona
import epoch from Std\Time in
epoch   # => 0
```

### `sleep : Int -> ()`

Sleep for the given number of milliseconds.

```yona
import sleep from Std\Time in
sleep 1000   # sleep for 1 second
```

### `format : Int -> String`

Format a millisecond timestamp as a human-readable date-time string.

```yona
import now, format from Std\Time in
format (now)   # => "2025-04-09 12:00:00"
```

### `elapsed : Int -> Int -> Int`

Compute the elapsed time in milliseconds between two timestamps.

```yona
import now, elapsed, sleep from Std\Time in
let t0 = now in
do
  sleep 100
  let t1 = now in
  elapsed t0 t1   # => ~100
end
```
