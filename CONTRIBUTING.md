# Contributing to Yona

Thank you for your interest in contributing to the Yona compiler!

## Development Setup

See [INSTALL.md](INSTALL.md) for build prerequisites. For development, use the debug preset:

```bash
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux
```

## Project Structure

```
yona/
  include/        C++ headers (AST, codegen, types, parser)
  src/            Compiler source (lexer, parser, codegen, runtime)
  src/runtime/    C runtime (seq, hamt, regex, platform layer)
  cli/            yonac CLI tool
  repl/           yona REPL
  lib/Std/        Standard library (.yona and .yonai modules)
  test/           Tests (doctest framework + codegen fixtures)
  bench/          Benchmarks with C reference implementations
  docs/           Documentation
```

## Making Changes

### Adding a Language Feature

Changes typically span: **Lexer** (new token) -> **Parser** (new AST node) -> **AST** (node definition in `ast.h`) -> **Codegen** (LLVM IR generation) -> **Tests** (codegen fixtures).

### Adding a Stdlib Module

1. Create `lib/Std/ModuleName.yona` (pure Yona) or implement C functions in `src/compiled_runtime.c`
2. Create `lib/Std/ModuleName.yonai` (interface file with function signatures)
3. Add test fixtures in `test/codegen/`
4. Generate API docs: `python3 scripts/gendocs.py`

### Adding a Runtime Function

1. Implement in `src/compiled_runtime.c` or a platform file
2. Add codegen declarations in `src/Codegen.cpp` (the `declare_runtime_functions` section)
3. Add function pointer in `include/Codegen.h`
4. If RC-managed, add destructor handler in `yona_rt_rc_dec`

## Code Style

- Run `./scripts/format.sh` before committing (clang-format)
- C++23 standard, clang compiler
- No unnecessary abstractions — keep it simple
- Test every change

## Testing

```bash
# Run all tests
./out/build/x64-debug-linux/tests

# Run a specific test
./out/build/x64-debug-linux/tests -tc="TestName"

# Run a specific subcase
./out/build/x64-debug-linux/tests -sc="subcase_name"

# Add a codegen E2E test
echo 'your expression' > test/codegen/test_name.yona
echo 'expected output' > test/codegen/test_name.expected
```

## Benchmarking

```bash
# Run all benchmarks with C comparison
python3 bench/runner.py --compare

# Add a new benchmark
echo 'your benchmark expression' > bench/category/bench_name.yona
echo 'expected output' > bench/category/bench_name.expected
# Optionally add C reference: bench/reference/bench_name.c
```

## Pull Request Guidelines

1. Create a feature branch from `master`
2. Ensure all tests pass (`ctest --preset unit-tests-linux`)
3. Ensure no benchmark regressions (`python3 bench/runner.py --compare`)
4. Run `./scripts/format.sh`
5. Write descriptive commit messages
6. Update documentation if the public API changes
