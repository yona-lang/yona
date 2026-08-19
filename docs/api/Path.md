# Std.Path

Path -- file path manipulation.

Pure string-based path operations for joining, splitting, and
inspecting file paths. No filesystem access is performed.

## Functions

### join

`join a b`

Join two path components with the platform separator.

```yona
import join from Std\Path in
join "/home/user" "docs"   # => "/home/user/docs"
```

### dirname

`dirname path`

Return the directory portion of a path.

```yona
import dirname from Std\Path in
dirname "/home/user/file.txt"   # => "/home/user"
```

### basename

`basename path`

Return the filename portion of a path.

```yona
import basename from Std\Path in
basename "/home/user/file.txt"   # => "file.txt"
```

### extension

`extension path`

Return the file extension including the dot.

```yona
import extension from Std\Path in
extension "photo.jpg"   # => ".jpg"
```

### withExtension

`withExtension path ext`

Replace the file extension with a new one.

```yona
import withExtension from Std\Path in
withExtension "data.csv" ".json"   # => "data.json"
```

### isAbsolute

`isAbsolute path`

Returns `true` if the path is absolute.

```yona
import isAbsolute from Std\Path in
isAbsolute "/usr/bin"   # => true
isAbsolute "src/main"   # => false
```
