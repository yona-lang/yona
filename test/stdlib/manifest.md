# Standard-library conformance manifest

Every public module in `docs/api/README.md` has an executable Yona suite. The
contract column names the primary behavior covered by that suite; focused
compiler/runtime fixtures supplement these public-API tests.

| Module | Tier | Script | Contracts |
|---|---|---|---|
| Std.Bool | pure | pure/Bool_test | truth tables and implication boundaries |
| Std.ByteArray | runtime | codecs/ByteArray_test | UTF-8 round trips, indexing, slicing, concatenation |
| Std.Channel | runtime | runtime/Channel_test | FIFO, close, capacity, empty receive |
| Std.Collection | pure | pure/Collection_test | iteration, replication, consecutive deduplication |
| Std.Convert | pure | foundation/Convert_test | total conversion, checked numeric conversion, structured parsing failures |
| Std.Crypto | runtime | codecs/Crypto_test | SHA-256 vector, random widths, UUID shape |
| Std.Dict | pure | pure/Core_test | persistent update, lookup, keys, size |
| Std.Encoding | runtime | codecs/Encoding_test | Base64, hex, URL, HTML, malformed input |
| Std.File | runtime | runtime/File_test | write, append, read, size, remove |
| Std.FloatArray | runtime | codecs/FloatArray_test | fill, persistent set, map, fold |
| Std.Format | pure | pure/Format_test | placeholders, order, brace escapes, extra arguments |
| Std.Function | pure | pure/Function_test | combinators, pipelines, fixed points |
| Std.Gpu | gpu | gpu/gpu_test | backend and capability probes |
| Std.Http | network | network/Http_test | response construction and parsing |
| Std.IntArray | runtime | codecs/IntArray_test | conversion, persistent set, map/filter/fold/slice |
| Std.Io | runtime | runtime/io_test | descriptor constants and tty query |
| Std.Iterator | pure | foundation/Iterator_test | stateful traversal, folds, native adapters, heap-element materialization |
| Std.Json | runtime | codecs/Json_test | nested parse/stringify, invalid syntax, scalars |
| Std.List | pure | pure/Core_test | map/filter/fold/slice/reverse/sort/zip/find |
| Std.Log | runtime | runtime/Log_test | level state |
| Std.Math | pure | pure/Core_test | signs, clamp, gcd, powers, factorial, parity |
| Std.Net | network | network/Net_test | invalid-descriptor safety |
| Std.Option | pure | pure/Option_test | empty/default/map/filter/zip |
| Std.Pair | pure | pure/Core_test | access, mapping, swap, tuple conversion |
| Std.Parallel | runtime | runtime/Parallel_test | ordered parallel map and completion |
| Std.Path | pure | codecs/Path_test | decomposition, extension, join, roots |
| Std.Process | runtime | runtime/Process_test | paths, arguments, version, environment |
| Std.Random | runtime | runtime/Random_test | bounds, singleton choice, shuffle cardinality |
| Std.Range | pure | pure/Core_test | endpoints, steps, slicing, fold, empty direction |
| Std.Regex | runtime | codecs/Regex_test | match groups, all matches, replacement, splitting |
| Std.Result | pure | pure/Result_test | maps, chaining, recovery, conversion, folding |
| Std.Set | pure | pure/Core_test | persistence, deduplication, union/intersection/difference |
| Std.String | pure | pure/Core_test | Unicode length, trim, substring, replace, join, parse |
| Std.Task | runtime | runtime/Task_test | native-promise task result |
| Std.Test | pure | framework/Test_test | cases, comparators, reporting, complete execution |
| Std.TraitLaws | pure | foundation/Traits_test | Eq, Ord, Hash, Show, Semigroup, and Monoid laws with counterexamples |
| Std.Time | runtime | runtime/Time_test | clocks, elapsed time, formatting, zero sleep |
| Std.Tuple | pure | pure/Core_test | access, mapping, curry/uncurry |
| Std.Types | pure | codecs/Types_test | integer, float, boolean conversion |
| Std.Utf16 | pure | codecs/Utf16_test | ASCII, surrogate pairs, CRLF positions |
