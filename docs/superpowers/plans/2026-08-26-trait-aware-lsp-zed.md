# Trait-Aware LSP and Zed Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` or `superpowers:executing-plans`
> to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Yona's foundational traits discoverable and navigable through
`yls`, and ship a first-class Zed client that launches that same server.

**Architecture:** Use the shared `SemanticModel` as the compiler-owned source
of stable binding identity and trait metadata, then project it through existing
LSP queries. Keep VS Code and Zed transport-only clients. Zed requires a
Tree-sitter grammar, so the implementation will publish a dedicated
`yona-lang/tree-sitter-yona` grammar repository and a dedicated
`yona-lang/zed-yona` extension repository, while the existing TextMate grammar
remains the VS Code/site syntax highlighter.

**Tech Stack:** C++23, existing parser/typechecker, doctest, LSP 3.17,
Tree-sitter, Rust/WASI Zed Extension API, TypeScript, CMake/Ninja, Python 3.

## Global Constraints

- Work directly on `master`; do not change `VERSION`.
- Keep `yona_semantics` as the only semantic authority; `yls` and editors do
  not implement separate binding resolution or typechecking.
- All LSP positions and edit ranges remain UTF-16 and end-exclusive.
- Source, imported `.yonai`, and Prelude contracts must behave consistently.
- Preserve full-document reparsing and recovered-prefix queries for malformed buffers.
- Keep the VS Code extension transport-only and its TextMate grammar synchronized with the site.
- Zed's grammar is Tree-sitter because Zed requires one; validate its lexical contract against Yona fixtures rather than pretending it is the TextMate grammar.
- Update `docs/todo-list.md`, this plan, `CHANGELOG.md`, relevant internal docs, and `site/src/content/docs/` in the same change.
- Use TDD, run the complete CMake/CTest suite, VS Code checks, Zed smoke checks, site build, and `git diff --check` before completion.

---

## Target file structure

```text
include/yona/Semantics/SemanticModel.h  # semantic identity + trait relationships
src/Lsp/Analysis.cpp                   # trait-aware index and query behavior
src/Lsp/Server.cpp                     # semantic-token legend/capabilities
test/Lsp/LspTest.cpp                      # protocol-level trait regressions
editors/tree-sitter-yona/              # publishable Tree-sitter grammar source
editors/zed/                           # publishable Zed extension source
scripts/check_zed_extension.py         # manifest/grammar/discovery package smoke
.github/workflows/cmake-multi-platform.yml
docs/superpowers/specs/2026-08-26-trait-lsp-zed-design.md
docs/superpowers/plans/2026-08-26-trait-aware-lsp-zed.md
docs/todo-list.md CHANGELOG.md docs/* site/src/content/docs/*
```

### Task 1: Give LSP occurrences stable trait-aware identities

**Files:**
- Modify: `include/yona/Semantics/SemanticModel.h`
- Modify: `src/Lsp/Analysis.cpp`
- Test: `test/Lsp/LspTest.cpp`

**Consumes:** `ast::TraitDeclNode::{name,type_params,methods,superclasses}` and
`ast::InstanceDeclNode::{trait_name,type_names,constraints,methods}`.

**Produces:** `semantics::SemanticOccurrence` fields `Binding`, `Detail`, and
`Container`; `Binding` is a strong model-local `BindingId`, while import origin
and display metadata remain separate values.

- [x] **Step 1: Write failing local trait-index tests.**

  Add a module with `trait Eq a; eq : a -> a -> Bool; end` and an `instance Eq
  Int`; assert document symbols contain `Eq` as `interface`, `eq` as
  `method`, and the instance as `instance`, with `Eq Int` detail and `Eq` as
  its container. Assert hover on each contains its category and complete
  signature/head.

- [x] **Step 2: Run the regression and confirm the current index lacks method
  and instance metadata.**

  Run: `./out/build/x64-debug-linux/tests -tc='LSP trait symbols*'`

  Expected: FAIL because `AST_TRAIT_DECL` only adds `Eq` and instance text is
  represented as an unrelated interface occurrence.

- [x] **Step 3: Implement the semantic model and AST walk.**

  Add detail, container, and strong `BindingId` data to the shared semantic
  occurrence. Add helpers that format:

  ```cpp
  trait Eq a where Ord a
  eq : a -> a -> Bool
  instance Eq Int
  instance (Eq a, Hash a) => Eq (Dict a String)
  ```

  In `AST_TRAIT_DECL`, add the trait definition and a method definition for
  every `TraitMethodSig`. In `AST_INSTANCE_DECL`, add a definition occurrence
  whose selection range is the trait name and whose identity includes the
  formatted head; walk methods with `container` set to that head. Make all
  name comparisons in definition/references/highlight/rename compare
  `BindingId`, not display spelling.

- [x] **Step 4: Run focused LSP tests.**

  Run: `cmake --build --preset build-debug-linux -j2 && ./out/build/x64-debug-linux/tests -tc='LSP trait symbols*'`

  Expected: PASS.

### Task 2: Expose trait semantics through all established LSP operations

**Files:**
- Modify: `src/Lsp/Analysis.cpp`
- Modify: `src/Lsp/Server.cpp`
- Test: `test/Lsp/LspTest.cpp`

**Consumes:** Task 1 semantic IDs and trait symbols.

**Produces:** trait-aware hover, signature help, completion detail, semantic
tokens, navigation, references, rename, and explain-instance code actions.

- [x] **Step 1: Add failing protocol regressions.**

  Add source tests that assert: `Ord` hover displays its superclass and
  `compare : a -> a -> Ordering`; an `Eq (Dict String Int)` occurrence defines
  to the local instance; references/rename of one `eq` method do not edit an
  unrelated method with the same spelling; completion emits LSP kind 8 for an
  interface and kind 2 for a method; semantic tokens classify interfaces as
  type and methods as function; signature help returns the complete method
  type. Add an E0105/E0106 missing/ambiguous-instance diagnostic fixture and
  assert its action title is `Explain trait instance E....` and invokes
  `yona.explain`.

- [x] **Step 2: Run the focused tests and confirm failures.**

  Run: `./out/build/x64-debug-linux/tests -tc='LSP trait operations*'`

  Expected: FAIL on method identity, completion kind/detail, and trait-action title.

- [x] **Step 3: Implement the query projections.**

  Render hover as `kind name`, then `detail`, then the declaration type;
  use `container` in document/workspace symbols and completion details. Map
  `interface` to LSP completion kind 8, `method` to 2, and `instance` to 5;
  add `method` to the semantic-token legend and map it to `function`.
  Restrict rename/ref/highlight/definition to matching semantic IDs. For a
  diagnostic whose message says `instance` or whose code is the trait
  selection code, emit the existing safe explain command with title
  `Explain trait instance <code>`; never synthesize an instance edit.

- [x] **Step 4: Verify request dispatch and commit.**

  Run: `cmake --build --preset build-debug-linux -j2 && ./out/build/x64-debug-linux/tests -tc='LSP trait operations*'`

  Expected: PASS.

  Commit: `git add src/Lsp/Analysis.cpp src/Lsp/Server.cpp test/Lsp/LspTest.cpp && git commit -m 'feat: expose trait semantics through yls'`

### Task 3: Cover imported contracts, UTF-16, and recovery

**Files:**
- Modify: `test/Lsp/LspTest.cpp`
- Modify: `scripts/ci/smoke_yls.py`

**Consumes:** Task 2 query behavior.

**Produces:** end-to-end proof that imported foundational contracts and
incomplete buffers preserve the same identity and UTF-16 ranges.

- [ ] **Step 1: Write failing import and incremental-change tests.**

  Create a temporary `.yonai` that exports an `Ord` method and `Ordering` ADT,
  open a consumer with an emoji before the imported use, and assert definition,
  hover, completion, and signature help use the interface declaration and
  UTF-16 position. Open a malformed trait module, replace it with a complete
  document through `didChange`, and assert stale parse diagnostics disappear
  while trait hover remains available on the recovered prefix before replacement.

- [ ] **Step 2: Run the test and confirm current behavior.**

  Run: `./out/build/x64-debug-linux/tests -tc='LSP imported trait contracts*'`

  Expected: FAIL if import origin propagation omits a trait method or a
  recovered declaration loses its semantic identity.

- [ ] **Step 3: Repair only the origin/recovery gap exposed by the test.**

  Ensure imported trait and method occurrences preserve `origin_module`,
  `origin_name`, and semantic identity through `propagate_origins`; make
  `.yonai` range lookup use the exported trait/method name. Do not special-case
  a standard-library module.

- [ ] **Step 4: Run C++ LSP coverage and commit.**

  Run: `cmake --build --preset build-debug-linux -j2 && ./out/build/x64-debug-linux/tests -tc='*LSP*'`

  Expected: PASS.

  Commit: `git add test/Lsp/LspTest.cpp scripts/ci/smoke_yls.py src/Lsp/Analysis.cpp && git commit -m 'test: cover imported trait contracts in yls'`

### Task 4: Ship a Zed language extension and Tree-sitter grammar

**Files:**
- Create: `grammar.js`, `package.json`, `src/parser.c`, `src/node-types.json`
- Create: `queries/highlights.scm`, `queries/brackets.scm`, `queries/outline.scm`, `queries/indents.scm`
- Create: `test/tree-sitter/traits.yona`, `test/tree-sitter/traits.expected`
- Create: `editors/zed/extension.toml`, `editors/zed/Cargo.toml`, `editors/zed/src/lib.rs`
- Create: `editors/zed/languages/yona/config.toml`, `editors/zed/languages/yona/highlights.scm`, `editors/zed/languages/yona/brackets.scm`, `editors/zed/languages/yona/outline.scm`, `editors/zed/languages/yona/indents.scm`, `editors/zed/languages/yona/semantic_token_rules.json`
- Create: `scripts/check_zed_extension.py`
- Modify: `.github/workflows/cmake-multi-platform.yml`

**Consumes:** the pinned `yona-lang/tree-sitter-yona` revision and `yls --stdio`
as the server command.

**Produces:** a locally installable/publishable Zed extension with deterministic
server discovery: configured `YONA_LSP_PATH`, `yls` in `PATH`, then
`$YONA_HOME/bin/yls`.

- [ ] **Step 1: Write grammar corpus and extension smoke checks.**

  Add a corpus covering modules/imports, `trait`, multi-parameter `instance`,
  `Ordering` constructors, ADTs, interpolation, comments, tuples/records,
  and malformed input. Make `check_zed_extension.py` parse TOML, require
  `Yona`/`.yona`/`.yonai`, verify all declared query files exist, require a
  full 40-character grammar revision, and assert Rust discovery order in
  `src/lib.rs`.

- [ ] **Step 2: Run checks and confirm they fail before the package exists.**

  Run: `python3 scripts/check_zed_extension.py`

  Expected: FAIL because `editors/zed/extension.toml` is absent.

- [ ] **Step 3: Implement the Tree-sitter grammar and queries.**

  Define named nodes for `module_declaration`, `import_declaration`,
  `trait_declaration`, `trait_method`, `instance_declaration`,
  `type_declaration`, `function_declaration`, `identifier`, `type_identifier`,
  literals, comments, delimiters, and a recovery `ERROR` path. Generate and
  commit `src/parser.c` and `node-types.json`; use the same keyword set as
  `Lexer::get_keywords`. Queries must highlight trait/type names as `@type`,
  methods/functions as `@function`, and provide bracket/outline/indent rules.

- [ ] **Step 4: Implement the Zed manifest and Rust host.**

  Use this exact manifest shape, replacing `GRAMMAR_REV` with the published
  grammar commit:

  ```toml
  id = "yona"
  name = "Yona"
  version = "0.1.6"
  schema_version = 1
  authors = ["Yona contributors"]
  description = "Yona language support powered by yls"
  repository = "https://github.com/yona-lang/zed-yona"

  [grammars.yona]
  repository = "https://github.com/yona-lang/tree-sitter-yona"
  rev = "GRAMMAR_REV"

  [language_servers.yls]
  name = "Yona Language Server"
  languages = ["Yona"]
  ```

  Implement `zed::Extension::language_server_command` to validate the server
  id and return `zed::Command { command, args: vec!["--stdio".into()], env }`.
  Resolve configured `YONA_LSP_PATH` first; otherwise `worktree.which("yls")`;
  otherwise `$YONA_HOME/bin/yls`; return a descriptive error when none exists.
  Reuse root query files in the language directory through generated copies
  with a check script so Zed files cannot silently diverge.

- [ ] **Step 5: Validate package, grammar, and CI, then commit.**

  Run: `tree-sitter test && python3 scripts/check_zed_extension.py && python3 scripts/check_zed_extension.py --check-generated && git diff --check`

  Expected: PASS. Add `python3 scripts/check_zed_extension.py --check-generated`
  to the Linux CI grammar step.

  Commit: `git add grammar.js package.json src/parser.c src/node-types.json queries test/tree-sitter editors/zed scripts/check_zed_extension.py .github/workflows/cmake-multi-platform.yml && git commit -m 'feat: add Zed language extension'`

### Task 5: Synchronize VS Code validation, documentation, and release readiness

**Files:**
- Modify: `editors/vscode/src/test/run.ts`, `editors/vscode/README.md`
- Modify: `docs/superpowers/specs/2026-08-26-trait-lsp-zed-design.md`
- Modify: `docs/superpowers/plans/2026-08-26-trait-aware-lsp-zed.md`
- Modify: `docs/todo-list.md`, `CHANGELOG.md`, `docs/language-server.md`
- Modify: `site/src/content/docs/guides/editor.md`, `site/src/content/docs/reference/cli.md`

**Consumes:** completed semantic capabilities and Zed package layout.

**Produces:** truthful user documentation and CI validation instructions,
with the finished roadmap item removed.

- [ ] **Step 1: Add VS Code contract checks.**

  Extend extension tests to assert the client advertises no semantic logic,
  sends only `yls --stdio`, recognizes `.yonai`, and keeps its grammar check.
  Do not add trait parsing to TypeScript.

- [ ] **Step 2: Add installation and publication documentation.**

  Document VS Code and Zed installation, exact server discovery order,
  `semantic_tokens: "combined"`, `YONA_LSP_PATH`, and Zed development install.
  State publication requires a public grammar commit, public extension Git
  repository, then a PR adding an HTTPS submodule and entry to
  `zed-industries/extensions`; no token or registry metadata is committed
  here. Correct the approved design's obsolete “canonical TextMate grammar”
  wording to distinguish TextMate from Tree-sitter.

- [ ] **Step 3: Mark records completed.**

  Mark every completed checkbox in this plan, set the design status to
  Implemented with the commit date, remove the completed trait-LSP/Zed item
  from `docs/todo-list.md`, and add an Unreleased changelog entry describing
  additive trait-aware `yls` support and Zed availability.

- [ ] **Step 4: Run all release gates and commit.**

  Run:

  ```bash
  cmake --preset x64-debug-linux
  cmake --build --preset build-debug-linux -j2
  ctest --preset unit-tests-linux --output-on-failure
  (cd editors/vscode && npm ci && npm test && npm run lint)
  python3 scripts/check_zed_extension.py --check-generated
  pnpm --dir site build
  git diff --check
  ```

  Expected: all commands PASS. If `clang-format` is unavailable, record its
  actionable environment limitation but do not report a false format pass.

  Commit: `git add editors/vscode docs site CHANGELOG.md && git commit -m 'docs: document Yona editor integrations'`

## Self-review

- **Spec coverage:** Tasks 1–3 cover source/imported trait contracts and all
  stated LSP operations, UTF-16, recovery, diagnostics, and JSON-RPC dispatch.
  Task 4 adds the mandatory Tree-sitter grammar, Zed package, discovery,
  semantic-token rules, and CI smoke. Task 5 keeps VS Code thin and updates
  all requested documentation, roadmap, and changelog records.
- **Corrected design constraint:** Zed cannot consume a TextMate grammar;
  its documented requirement is a Tree-sitter grammar. The plan therefore
  uses a dedicated public Tree-sitter grammar repository and preserves
  TextMate only for VS Code/site highlighting.
- **No placeholders:** every created artifact, interface, test target, and
  verification command is named. `GRAMMAR_REV` is resolved only after the
  grammar commit exists, so the checked-in manifest receives an immutable
  forty-character SHA rather than a branch name.
- **Type consistency:** `BindingId`, `Detail`, and `Container` are produced
  in Task 1 and are the only trait identity inputs used by Task 2/3. Zed
  always launches the existing `yls --stdio` and never imports LSP C++ code.
