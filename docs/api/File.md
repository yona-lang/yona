# Std.File

File -- filesystem operations with async I/O support.

Provides file reading, writing, directory listing, and low-level
file handle operations. Async functions (`readFile`, `readFileBytes`,
`readBytes`, `writeBytes`) use io_uring on Linux for non-blocking I/O.

## Functions

### readFile

`readFile path`

Read the entire contents of a file as a string. Async (io_uring).

```yona
import readFile from Std\File in
let contents = readFile "data.txt" in
println contents
```

### writeFile

`writeFile path contents`

Write a string to a file, creating or overwriting it. Async (io_uring).
Returns `true` on success.

```yona
import writeFile from Std\File in
writeFile "out.txt" "hello world"   # => true
```

### appendFile

`appendFile path contents`

Append a string to a file. Returns `true` on success.

```yona
import appendFile from Std\File in
appendFile "log.txt" "new line\n"   # => true
```

### exists

`exists path`

Check whether a file or directory exists at the given path.

```yona
import exists from Std\File in
exists "/tmp"   # => true
```

### remove

`remove path`

Delete a file. Returns `true` on success.

```yona
import remove from Std\File in
remove "temp.txt"   # => true
```

### size

`size path`

Returns the size of a file in bytes.

```yona
import size from Std\File in
size "data.bin"   # => 4096
```

### listDir

`listDir path`

List directory contents. Returns a sequence of filenames.

```yona
import listDir from Std\File in
listDir "/tmp"   # => ["file1.txt", "file2.txt", ...]
```

### readLines

`readLines path`

Returns an `Iterator String` that yields lines from the file lazily.
Uses O(1) memory per element.

```yona
import readLines from Std\File in
let iter = readLines "big.csv" in
# consume with iterator protocol
```

### readFileBytes

`readFileBytes path`

Read the entire file as a byte buffer. Async (io_uring).

```yona
import readFileBytes from Std\File in
let buf = readFileBytes "image.png" in
Bytes::length buf
```

### writeFileBytes

`writeFileBytes path bytes`

Write a byte buffer to a file. Returns `true` on success.

```yona
import writeFileBytes from Std\File in
import fromSeq from Std\ByteArray in
writeFileBytes "out.bin" (fromSeq [0, 1, 2, 3])
```

### openFile

`openFile path mode`

Open a file with the given mode string (`"r"`, `"w"`, `"rw"`, etc.).
Returns a file descriptor (Int).

```yona
import openFile, closeFileHandle from Std\File in
let fd = openFile "data.txt" Read in
closeFileHandle fd
```

The mode is a `FileMode` ADT (Prelude): `Read`, `Write`, `ReadWrite`, `Append`.

### closeFileHandle

`closeFileHandle fd`

Close a file descriptor.

### readBytes

`readBytes fd count`

Read up to `count` bytes from a file descriptor. Async (io_uring).
Returns a byte buffer.

### writeBytes

`writeBytes fd count`

Write bytes to a file descriptor. Async (io_uring).
Returns the number of bytes written.

### seek

`seek fd offset whence`

Seek to a position in a file. `whence` is a `Whence` ADT (Prelude):
`SeekSet` (absolute), `SeekCur` (relative to current), `SeekEnd` (relative to end).
Returns the new position.

```yona
import openFile, seek, tell from Std\File in
let fd = openFile "data.bin" "r" in
seek fd 100 "set"
```

### tell

`tell fd`

Returns the current position in a file descriptor.

### flush

`flush fd`

Flush buffered writes for a file descriptor. Returns `true` on success.

### truncate

`truncate fd length`

Truncate a file to the given length. Returns `true` on success.

### readChunks

`readChunks fd chunkSize`

Read data from a file descriptor in chunks of `chunkSize` bytes.
Returns a handle for chunked reading.
