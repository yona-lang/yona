# Std.Crypto

Crypto -- cryptographic hashing and random byte generation.

Provides SHA-256 hashing, cryptographically secure random bytes,
and UUID v4 generation.

## Functions

### `sha256 : String -> String`

Compute the SHA-256 hash of a string. Returns the hex-encoded digest.

```yona
import sha256 from Std\Crypto in
sha256 "hello"   # => "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
```

### `randomBytes : Int -> ByteArray`

Generate `n` cryptographically secure random bytes as binary data. Non-positive
lengths return an empty `ByteArray`.

```yona
import randomBytes from Std\Crypto, length from Std\ByteArray in
let key = randomBytes 32 in
length key   # => 32
```

### `randomHex : Int -> String`

Generate `n` random bytes and return them as a hex-encoded string (2n characters).

```yona
import randomHex from Std\Crypto in
randomHex 16   # => "a3f2b1..." (32 hex characters)
```

### `uuid4 : String`

Generate a random UUID v4 string.

```yona
import uuid4 from Std\Crypto in
uuid4   # => "550e8400-e29b-41d4-a716-446655440000"
```
