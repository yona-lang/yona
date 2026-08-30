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
  src/Runtime/    Componentized C runtime
  cli/            yonac CLI tool
  repl/           yona REPL
  lib/Std/        Standard library (.yona and .yonai modules)
  test/           Tests (doctest framework + codegen fixtures)
  bench/          Benchmarks with C reference implementations
  docs/           Documentation
```

## Making Changes

### Adding a Language Feature

Changes typically span: **Lexer** (new token) -> **Parser** (new AST node) ->
**AST** (node definition in `include/yona/Syntax/Ast.h`) -> **Codegen** (LLVM
IR generation) -> **Tests** (codegen fixtures).

### Adding a Stdlib Module

1. Create `lib/Std/ModuleName.yona` (pure Yona) or implement unavoidable
   native functions in the owning `src/Runtime/` component
2. Create `lib/Std/ModuleName.yonai` (interface file with function signatures)
3. Add test fixtures in `test/Fixtures/Codegen/`
4. Generate API docs: `python3 scripts/gendocs.py`

### Adding a Runtime Function

1. Implement in the owning runtime component (use `src/Runtime/Platform/`
   only for OS-specific behavior)
2. Add codegen declarations in `src/Codegen/Codegen.cpp`
   (`Codegen::declareRuntime`)
3. Add function pointer in `include/yona/Codegen/Codegen.h`
4. If RC-managed, add destructor handler in `YonaRuntimeRelease`

## Code Style

- Follow the canonical naming, path, header guard, and include conventions in
  [docs/quality.md](docs/quality.md).
- Run `python scripts/quality.py format` before committing.
- Run `python scripts/quality.py format-check`, `hygiene`, and `naming` before
  submitting a change. Pre-commit runs the same local checks on staged files.
- Use the C++23 standard and LLVM 22.1.x formatting and analysis tools.
- Keep abstractions purposeful and test every behavior change.

## Testing

```bash
# Run all tests
./out/build/x64-debug-linux/tests

# Run a specific test
./out/build/x64-debug-linux/tests -tc="TestName"

# Run a specific subcase
./out/build/x64-debug-linux/tests -sc="subcase_name"

# Add a codegen E2E test
echo 'your expression' > test/Fixtures/Codegen/test_name.yona
echo 'expected output' > test/Fixtures/Codegen/test_name.expected
```

## Benchmarking

```bash
# Run all benchmarks with C comparison
python3 bench/runner.py --compare

# Add a new benchmark
echo 'your benchmark expression' > bench/category/bench_name.yona
echo 'expected output' > bench/category/bench_name.expected
# Optionally add C reference: bench/Reference/BenchName.c
```

## Pull Request Guidelines

1. Create a feature branch from `master`
2. Ensure all tests pass (`ctest --preset unit-tests-linux`)
3. Ensure no benchmark regressions (`python3 bench/runner.py --compare`)
4. Run `python scripts/quality.py quality --build-dir <configured-build-dir>`
5. Write descriptive commit messages
6. Update documentation if the public API changes
