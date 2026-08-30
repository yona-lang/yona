# Linear Resource APIs Design

## Goal

Make ownership real and uniform for file handles, processes, TCP/UDP sockets,
and channel endpoints. Public APIs must expose typed payloads protected by
`Linear`; no public operation may accept a raw resource `Int` or pointer.

## Public representation

Each resource keeps a distinct opaque payload ADT:

- `FileHandle`
- `Process`
- `Socket`
- `Sender a` and `Receiver a`

Creation returns `Linear Payload`: for example, `openFile : String -> FileMode
-> Linear FileHandle`, `spawn : String -> Linear Process`, network constructors
return `Linear Socket`, and `channel : Int -> (Linear (Sender a), Linear
(Receiver a))`.

The payload itself implements `Closeable`. Users consume the wrapper with
`with` or `case Linear resource -> ...`; the wrapper is a linearity fact, not a
second runtime resource representation.

## Ownership transitions

An operation that leaves a resource usable consumes its payload and returns it
alongside its result. Examples are file reads/writes/seeks, process I/O and
wait-status polling, socket send/receive, and channel send/receive. A terminal
operation (`close`, process wait/kill when it releases the process, endpoint
close) consumes the payload and returns only its result. `with` closes the
payload on every exit path.

No overload accepting a raw fd, process pointer, or channel pointer remains in
the public standard library. Runtime C helpers may use raw values internally
after extracting them from the typed payload ADT.

## Compiler and interface requirements

`.yonai` must first gain recursive type descriptors so it can preserve both the
`LINEAR` outer marker and the inner ADT type (for example
`LINEAR(ADT(FileHandle))`). The interface loader and `ImportedFnSig` must carry
the descriptor into the type checker; marker-only `LINEAR` metadata is not a
sound basis for typed resource ownership.
The linearity checker treats each returned wrapper as a resource obligation;
using it after a consuming transition is E0600 and dropping it is E0602. The
existing `with` cleanup path discharges the final obligation. Leaks become
hard errors for these resource constructors so a program cannot compile while
silently discarding an OS resource.

## Runtime boundary

The C runtime owns payload allocation and extraction. It exposes resource-aware
entry points matching the standard-library ABI; typed Yona declarations—not
raw integer declarations—form the only public boundary. Platform networking
and process implementations retain their native fd/handle representation.

## Canonical boundary

This is intentionally breaking. `stdinFd`, `stdoutFd`, `stderrFd`, raw
channel helpers, and public resource functions typed with `Int` are removed or
made private implementation details. Standard-library code and fixtures move to
the transfer-returning APIs in the same change.

## Verification

Tests cover every resource family:

- creation returns `Linear` with its precise payload type;
- a non-terminal operation returns the resource for the next use;
- use-after-consume fails E0600 in expression programs and module bodies;
- leaking a created resource fails E0602;
- `with` closes resources and discharges the obligation; and
- platform runtime tests retain file/process/network/channel behavioral
  coverage through the typed ABI.

Documentation updates include the linear types guide, generated File/Process/
Net/Channel API pages, and the public Memory, concurrency, traits, and type
system pages.
