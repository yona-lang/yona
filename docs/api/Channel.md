# Std.Channel

Std\Channel — bounded MPMC channels with type-safe sender/receiver split.

`channel n` returns `(Linear (Sender a), Linear (Receiver a))`. The
linearity checker enforces that each side is unwrapped from `Linear`
exactly once via pattern match. After unwrapping, a `Sender a` can only
`send` and a `Receiver a` can only `recv` / `tryRecv`, enforcing the
producer/consumer split at the type level.

See `docs/api/Channel.md` for the full API.

## Types

### `type Sender a = Sender Channel`

Send-only handle wrapping a Channel.

### `type Receiver a = Receiver Channel`

Receive-only handle wrapping a Channel.

## Functions

### `extern`

```yona
extern raw_new      : Int -> Channel        = "yona_Std_Channel__raw_new"
```

Low-level externs to the C runtime wrappers. The `= "..."` form binds
a Yona-friendly local name to the mangled C ABI symbol so the wrapper
bodies below stay readable.

### `extern`

```yona
extern raw_send     : Channel -> Int -> ()  = "yona_Std_Channel__raw_send"
```

### `extern`

```yona
extern raw_recv     : Channel -> Option     = "yona_Std_Channel__raw_recv"
```

### `extern`

```yona
extern raw_try_recv : Channel -> Option     = "yona_Std_Channel__raw_tryRecv"
```

### `extern`

```yona
extern raw_close    : Channel -> ()         = "yona_Std_Channel__raw_close"
```

### `extern`

```yona
extern raw_closed   : Channel -> Bool       = "yona_Std_Channel__raw_isClosed"
```

### `extern`

```yona
extern raw_length   : Channel -> Int        = "yona_Std_Channel__raw_length"
```

### `extern`

```yona
extern raw_capacity : Channel -> Int        = "yona_Std_Channel__raw_capacity"
```

### `channel`

```yona
channel n =
```

Create a bounded channel with the given buffer capacity. Returns a tuple
of `Linear`-wrapped sender and receiver handles. The linearity checker
requires both wrappers to be unwrapped before scope exit:

```yona
let (sl, rl) = channel 16 in
case sl of Linear sender ->
case rl of Linear receiver ->
...                 -- use sender / receiver freely
end end
```

### `send`

```yona
send s v = raw_send s v
```

Send a value through a sender. Blocks if the buffer is full.

### `recv`

```yona
recv r = raw_recv r
```

Receive a value. Blocks if the buffer is empty. Returns `Some v` for a
delivered value or `None` once the channel is closed and drained.

### `tryRecv`

```yona
tryRecv r = raw_try_recv r
```

Non-blocking receive — returns immediately even if empty.

### `close`

```yona
close s = raw_close s
```

Close the sender side. Wakes all blocked sends and recvs.

### `isClosed`

```yona
isClosed s = raw_closed s
```

Returns true if the channel has been closed.

### `length`

```yona
length s = raw_length s
```

Current number of buffered elements.

### `capacity`

```yona
capacity s = raw_capacity s
```

Maximum buffer size (set at creation).

