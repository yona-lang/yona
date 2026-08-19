# Std.Channel

Std\Channel — bounded MPMC channels with type-safe sender/receiver split.

`channel n` returns `(Linear (Sender a), Linear (Receiver a))`. The
linearity checker enforces that each side is unwrapped from `Linear`
exactly once via pattern match. After unwrapping, a `Sender a` can only
`send` and a `Receiver a` can only `recv` / `tryRecv`, enforcing the
producer/consumer split at the type level.

See `docs/api/Channel.md` for the full API.

## Types

### Sender

`type Sender a = Sender Channel`

Send-only handle wrapping a Channel.

### Receiver

`type Receiver a = Receiver Channel`

Receive-only handle wrapping a Channel.

## Functions

### `channel : Int -> (Linear (Sender a), Linear (Receiver a))`

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

### `send : Sender a -> a -> ()`

Send a value through a sender. Blocks if the buffer is full.

### `recv : Receiver a -> Option a`

Receive a value. Blocks if the buffer is empty. Returns `Some v` for a
delivered value or `None` once the channel is closed and drained.

### `tryRecv : Receiver a -> Option a`

Non-blocking receive — returns immediately even if empty.

### `close : Sender a -> ()`

Close the sender side. Wakes all blocked sends and recvs.

### `isClosed : Sender a -> Bool`

Returns true if the channel has been closed.

### `length : Sender a -> Int`

Current number of buffered elements.

### `capacity : Sender a -> Int`

Maximum buffer size (set at creation).
