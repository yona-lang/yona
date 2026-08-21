import * as assert from "node:assert";
import * as fs from "node:fs";
import * as path from "node:path";
import { resolveYlsPath, which } from "../ylsPath";

function testResolvePrefersConfigured(): void {
  const configured = path.join(__dirname, "does-not-need-to-exist-but-is-used");
  assert.strictEqual(resolveYlsPath(configured), configured);
}

function testGrammarHasRequiredTokens(): void {
  const grammarPath = path.join(__dirname, "..", "..", "syntaxes", "yona.tmLanguage.json");
  const text = fs.readFileSync(grammarPath, "utf8");
  assert.ok(!text.includes('"daemon"'), "legacy daemon keyword must not be highlighted");
  for (const tok of ["fun", "lambda", "record", "deriving", "native", "io", "@borrow"]) {
    assert.ok(text.includes(tok), `grammar must include ${tok}`);
  }
}

function testWhichRejectsShellMetacharacters(): void {
  assert.strictEqual(which("yls;id"), undefined);
}

function testPackagingMetadata(): void {
  const root = path.join(__dirname, "..", "..");
  const pkg = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8")) as {
    publisher?: string;
    scripts?: Record<string, string>;
    devDependencies?: Record<string, string>;
  };
  assert.strictEqual(pkg.publisher, "yona-lang");
  assert.ok(pkg.scripts?.package?.includes("vsce"), "package script must run vsce");
  assert.ok(pkg.scripts?.vsix?.includes("vsce") || pkg.scripts?.vsix?.includes("package"),
    "vsix script must produce a VSIX");
  // out/ is gitignored; lint is --noEmit. CI and a clean clone run npm test
  // next, so the test script itself must emit JS before node out/test/run.js.
  const testScript = pkg.scripts?.test ?? "";
  assert.ok(
    /(?:^|[;&]\s*)(?:npm run compile|tsc(?:\s|$))/.test(testScript) &&
      !/--noEmit/.test(testScript),
    "test script must compile TypeScript (not --noEmit) before running out/test/run.js",
  );
  assert.ok(pkg.devDependencies?.["@vscode/vsce"], "@vscode/vsce is required to package locally");
  assert.ok(pkg.devDependencies?.ovsx, "ovsx is required so a human can publish to Open VSX later");
  assert.ok(fs.existsSync(path.join(root, "README.md")), "README.md required for vsce");
  assert.ok(
    fs.existsSync(path.join(root, "LICENSE")) || fs.existsSync(path.join(root, "LICENSE.txt")),
    "LICENSE required for vsce"
  );
}

function main(): void {
  testResolvePrefersConfigured();
  testGrammarHasRequiredTokens();
  testWhichRejectsShellMetacharacters();
  testPackagingMetadata();
  console.log("vscode extension unit tests passed");
}

main();
