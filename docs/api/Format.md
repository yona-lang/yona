# Std.Format

Format -- string formatting with positional placeholders.

Provides printf-style string formatting using `{}` placeholders
filled from a sequence of values.

## Functions

### format

`format : String -> [a] -> String`

Format a template string by replacing `{}` placeholders with values
from the `args` sequence, in order.

```yona
import format from Std\Format in
format "Hello, {}! You are {} years old." ["Alice", "30"]
# => "Hello, Alice! You are 30 years old."
```
