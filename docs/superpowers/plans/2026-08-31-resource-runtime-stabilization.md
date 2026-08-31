# Resource Runtime Stabilization Implementation Plan

> Execute with subagent-driven development. Start each production task from a
> focused failing regression, review every commit independently, and defer
> todo/changelog closure to the user-requested combined final update.

**Goal:** Align channel and file runtime representations with their canonical
typed interfaces, without weakening linear resource ownership or raw-descriptor
APIs used by console/LSP code.

**Architecture:** Runtime values must match the semantic constructors promised
by `.yonai`: channel creation returns a tuple of two `Linear` wrappers around
typed endpoints, and file operations accept the unwrapped `FileHandle` payload.
Raw integer descriptors remain the explicit `Std\Io` boundary for console,
pipe, socket, and LSP framing; handle-based exact reads belong to `Std\File`.
The same internal C implementation may serve both entry-point families, but
source modules never pretend one HM parameter is `Int | FileHandle`.

## Task 1: Return canonical Linear channel endpoints

**Files:**

- Modify: `src/Runtime/Concurrency/ChannelPosix.c`
- Modify: `src/Runtime/Concurrency/ChannelWin32.c`
- Modify: focused runtime/channel tests under `test/Runtime/`
- Modify only as needed for typed fixture controls: `test/Fixtures/Codegen/channel_*.yona`

- [ ] **Step 1: Add runtime-shape and E2E RED coverage**

Assert `YonaStdChannelChannel` returns
`(Linear (Sender raw), Linear (Receiver raw))` with every heap mask and
reference count owning exactly its child. Retain `channel_basic`, which
currently reaches `YonaStdChannelRawSend(0, 42)` after double-unwrapping the
runtime's missing layers. Add an explicit payload annotation only to fixtures
whose payload truly remains unconstrained.

- [ ] **Step 2: Add the missing runtime wrappers on both platforms**

Allocate `Sender` and `Receiver` endpoint ADTs around the shared channel, then
allocate one `Linear` ADT around each endpoint before placing them in the
tuple. Balance every allocation/retain/release path, including partial
allocation failure. Keep payload descriptors, blocking semantics, and raw
send/receive entry points unchanged.

- [ ] **Step 3: Verify ownership and commit**

Run direct runtime tests plus basic, capacity, heap-payload, close/deadlock,
spawn/pipeline, and GPU channel fixtures. Run allocation-stat controls. Commit
as `fix: return linear channel endpoints`.

## Task 2: Give Std File operations typed handle contracts

**Files:**

- Modify: `lib/Std/File.yonai`
- Modify: `test/Fixtures/Codegen/binary_*.yona`
- Modify: `test/Fixtures/Codegen/linear_file_case.yona`
- Modify: semantic/interface and codegen tests under `test/`

- [ ] **Step 1: Add exact contract RED coverage**

Assert `openFile` returns `LINEAR(ADT(FileHandle))`; close consumes an
`ADT(FileHandle)`; read/write/seek/tell/flush/truncate borrow the unwrapped
handle; binary payloads use `BYTE_ARRAY`; and mode/whence parameters retain
their ADT descriptors. Add a negative type test proving a `Linear FileHandle`
cannot be passed directly to a payload operation.

- [ ] **Step 2: Repair the canonical native interface**

Replace legacy `INT` guesses in `Std\File.yonai` with the precise Prelude ADT,
ByteArray, FileMode, Whence, Iterator, and result descriptors. Keep the C ABI
entry points unchanged: their existing `fhFd` helper already extracts either
layout, while the public typed boundary requires one explicit source-level
`Linear` unwrap. Mark non-terminal handle parameters borrowed; do not mark
close borrowed.

- [ ] **Step 3: Make fixtures obey linear ownership**

Rewrite binary fixtures to unwrap each newly opened handle exactly once before
borrowing it for operations and finally closing it. Do not weaken the checker
or rely on implicit `Linear` coercions.

- [ ] **Step 4: Verify and commit**

Run canonical import/type tests, linearity/resource tests, and all File/binary
fixtures on Linux. Commit as `fix: type file handle operations`.

## Task 3: Put exact reads on the correct resource boundary

**Files:**

- Modify: `src/Runtime/Stdlib/Native.c`
- Modify: `lib/Std/File.yonai`
- Modify: `lib/Std/Io.yona`
- Modify: `test/Fixtures/Codegen/stdlib_io_readexact.yona`
- Modify: focused semantic/codegen tests
- Regenerate/update as required: `docs/api/File.md`, `docs/api/Io.md`

- [ ] **Step 1: Lock both source-level contracts**

Retain `Std\Io.readExact`/`readExactBytes` for raw `Int` descriptors such as
`stdinFd`; remove the inaccurate claim that they also accept typed handles.
Add `Std\File.readExact`/`readExactBytes` accepting an unwrapped `FileHandle`,
and prove the wrong representation is rejected for each module boundary.

- [ ] **Step 2: Reuse the representation-aware C helpers**

Factor the existing exact-read implementation behind explicit Io-fd and
FileHandle C entry points, or add narrow File aliases if that keeps the runtime
clear. Keep distinct module declarations so HM typing remains sound; do not
introduce a wildcard parameter or implicit coercion. Update the fixture to
unwrap its file handle and import the typed exact-byte read from `Std\File`,
while retaining the zero-length stdin descriptor control from `Std\Io`.

- [ ] **Step 3: Regenerate, verify, and commit**

Regenerate only owned API outputs; inspect for unrelated churn. Run Io,
FileHandle, LSP framing, and fixture tests. Commit as
`feat: add FileHandle exact reads`.

## Task 4: Close the resource batch after full reassessment

**Files:**

- Modify: this plan
- Modify: `docs/todo-list.md`
- Modify: `docs/superpowers/specs/2026-08-31-open-bug-stabilization-design.md`
- Modify: `CHANGELOG.md`
- Modify: `.superpowers/sdd/progress.md`
- Modify public File/Io/Channel documentation if contracts changed

- [ ] **Step 1: Run focused and full Linux gates**

Build the debug preset; run channel/runtime, file/binary, Io/LSP, linearity,
effect-row, and full fixture suites; then run the full CTest preset and
`git diff --check`.

- [ ] **Step 2: Record combined results later**

Close the channel and FileHandle bugs only with passing evidence. Reverify the
effectful-file item: reviewed semantic-signature work already makes its focused
`Fs.read` cases pass, so close it as a fixed cascade rather than inventing an
extra implementation change. Include all results in the final combined
stabilization documentation update.
