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

function main(): void {
  testResolvePrefersConfigured();
  testGrammarHasRequiredTokens();
  testWhichRejectsShellMetacharacters();
  console.log("vscode extension unit tests passed");
}

main();
