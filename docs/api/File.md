# Std.File

File -- filesystem operations with async I/O support.

Provides file reading, writing, directory listing, and low-level
file handle operations. Async functions (`readFile`, `readFileBytes`,
`readBytes`, `writeBytes`) use io_uring on Linux for non-blocking I/O.

## Functions

### `readFile : String -> String`

Read the entire contents of a file as a string. Async (io_uring).

```yona
import readFile from Std\File in
let contents = readFile "data.txt" in
println contents
```

### `writeFile : String -> String -> Bool`

Write a string to a file, creating or overwriting it. Async (io_uring).
Returns `true` on success.

```yona
import writeFile from Std\File in
writeFile "out.txt" "hello world"   # => true
```

### `appendFile : String -> String -> Bool`

Append a string to a file. Returns `true` on success.

```yona
import appendFile from Std\File in
appendFile "log.txt" "new line\n"   # => true
```

### `exists : String -> Bool`

Check whether a file or directory exists at the given path.

```yona
import exists from Std\File in
exists "/tmp"   # => true
```

### `remove : String -> Bool`

Delete a file. Returns `true` on success.

```yona
import remove from Std\File in
remove "temp.txt"   # => true
```

### `size : String -> Int`

Returns the size of a file in bytes.

```yona
import size from Std\File in
size "data.bin"   # => 4096
```

### `listDir : String -> [String]`

List directory contents. Returns a sequence of filenames.

```yona
import listDir from Std\File in
listDir "/tmp"   # => ["file1.txt", "file2.txt", ...]
```

### `readLines : String -> Iterator String`

Returns an `Iterator String` that yields lines from the file lazily.
Uses O(1) memory per element.

```yona
import readLines from Std\File in
let iter = readLines "big.csv" in
# consume with iterator protocol
```

### `readFileBytes : String -> ByteArray`

Read the entire file as a byte buffer. Async (io_uring).

```yona
import readFileBytes from Std\File in
let buf = readFileBytes "image.png" in
Bytes::length buf
```

### `writeFileBytes : String -> ByteArray -> Bool`

Write a byte buffer to a file. Returns `true` on success.

```yona
import writeFileBytes from Std\File in
import fromSeq from Std\ByteArray in
writeFileBytes "out.bin" (fromSeq [0, 1, 2, 3])
```

### `openFile : String -> FileMode -> Linear FileHandle`

Open a file with the given mode string (`"r"`, `"w"`, `"rw"`, etc.).
It returns an owning `Linear FileHandle`, not a raw descriptor. Match the
wrapper once to access the opaque handle and close it:

```yona
import openFile, closeFileHandle from Std\File in
case openFile "data.txt" Read of
    Linear file -> closeFileHandle file
end
```

The mode is a `FileMode` ADT (Prelude): `Read`, `Write`, `ReadWrite`, `Append`.

### `closeFileHandle : FileHandle -> ()`

Close a file handle.

### `readBytes : FileHandle -> Int -> ByteArray`

Read up to `count` bytes from a file handle. Async (io_uring).
Returns a byte buffer.

### `readExact : FileHandle -> Int -> Result (String, String)`

Read exactly `count` bytes from an unwrapped typed file handle. Returns `Ok`
with the bytes, or `Err` if EOF arrives early or the count is invalid.

### `readExactBytes : FileHandle -> Int -> String`

Read up to `count` bytes from an unwrapped typed file handle, returning a short
string at EOF. Use `Std\Io.readExactBytes` for raw pipe, socket, or console
descriptors.

### `writeBytes : FileHandle -> ByteArray -> Int`

Write bytes to a file handle. Async (io_uring).
Returns the number of bytes written.

### `seek : FileHandle -> Int -> Whence -> Int`

Seek to a position in a file. `whence` is a `Whence` ADT (Prelude):
`SeekSet` (absolute), `SeekCur` (relative to current), `SeekEnd` (relative to end).
Returns the new position.

```yona
import openFile, seek, tell from Std\File in
case openFile "data.bin" Read of
    Linear file -> seek file 100 SeekSet
end
```

### `tell : FileHandle -> Int`

Returns the current position in a file handle.

### `flush : FileHandle -> Bool`

Flush buffered writes for a file handle. Returns `true` on success.

### `truncate : FileHandle -> Int -> Bool`

Truncate a file to the given length. Returns `true` on success.

### `readChunks : FileHandle -> Int -> Iterator ByteArray`

Read data from a file handle in chunks of `chunkSize` bytes.
Returns a handle for chunked reading.
