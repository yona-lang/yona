#!/usr/bin/env node
/**
 * Import the generated stdlib API reference from this repository's
 * docs/api/ (produced by scripts/gendocs.py) into src/content/docs/stdlib/
 * with Starlight frontmatter. Maintainers: do not edit those pages by
 * hand — they are overwritten by this script. That note is not shown
 * on the published pages.
 *
 * Usage: node scripts/sync-stdlib.mjs
 *        (run from site/, or npm run sync)
 */
import {
  readdirSync,
  readFileSync,
  writeFileSync,
  mkdirSync,
  rmSync,
  existsSync,
} from "node:fs";
import { dirname, join, basename } from "node:path";
import { fileURLToPath } from "node:url";

const SCRIPT_DIR = dirname(fileURLToPath(import.meta.url));
const SITE_ROOT = join(SCRIPT_DIR, "..");
const REPO_ROOT = join(SITE_ROOT, "..");
const API_DIR = join(REPO_ROOT, "docs", "api");
const OUT_DIR = join(SITE_ROOT, "src", "content", "docs", "stdlib");

if (!existsSync(API_DIR)) {
  console.error(
    `sync-stdlib: missing ${API_DIR} — run python3 scripts/gendocs.py from the repo root first`,
  );
  process.exit(1);
}

const yamlQuote = (s) => `'${s.replaceAll("'", "''")}'`;

function rewriteLinks(text) {
  return text.replace(
    /\]\(([A-Za-z0-9_]+)\.md(#[^)]*)?\)/g,
    (_, mod, anchor) => {
      return `](/stdlib/${mod.toLowerCase()}/${anchor ?? ""})`;
    },
  );
}

function compactSignatures(text) {
  let next = text.replace(
    /^### `([^`]+)`\n\n```(?:yona)?\n([\s\S]*?)\n```\n/gm,
    (m, name, body) => {
      if (name.includes(":")) return m;
      const first = body
        .trim()
        .split("\n")[0]
        .replace(/\s*=\s*$/, "");
      return `### ${name}\n\n\`${first}\`\n\n`;
    },
  );
  next = next.replace(
    /^### `type ([A-Z][a-zA-Z0-9_]*)([^`]*)`\n/gm,
    "### $1\n\n`type $1$2`\n",
  );
  return next.replace(/\n{3,}/g, "\n\n");
}

function liftSignatures(text) {
  return text.replace(
    /^### ([a-z][a-zA-Z0-9_]*)\n\n`([^`]+)`\n/gm,
    (m, name, sig) => {
      if (!sig.includes(":")) return m;
      const full = sig.startsWith(name) ? sig : `${name} : ${sig}`;
      return `### \`${full}\`\n`;
    },
  );
}

function tagBareFences(text) {
  const lines = text.split("\n");
  let inFence = false;
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i].match(/^```(.*)$/);
    if (!m) continue;
    if (!inFence) {
      if (m[1].trim() === "") lines[i] = "```yona";
      inFence = true;
    } else {
      inFence = false;
    }
  }
  return lines.join("\n");
}

function transform(raw, { isIndex }) {
  let text = raw.replace(/\r\n/g, "\n");

  const headingMatch = text.match(/^#\s+(.+)\n/);
  const heading = headingMatch ? headingMatch[1].trim() : "Standard library";
  text = headingMatch ? text.slice(headingMatch[0].length) : text;

  const para = text
    .split(/\n\s*\n/)
    .map((p) => p.trim())
    .find(
      (p) =>
        p && !p.startsWith("#") && !p.startsWith("```") && !p.startsWith("|"),
    );
  const description = (para ?? "")
    .replace(/\s+/g, " ")
    .replace(/[*_`|]/g, "")
    .slice(0, 160);

  const title = isIndex ? "Standard library" : heading.replace("Std.", "Std\\");

  text = liftSignatures(tagBareFences(rewriteLinks(compactSignatures(text))));

  return `---\ntitle: ${yamlQuote(title)}\ndescription: ${yamlQuote(description)}\n---\n\n<div class="stdlib-api">\n\n${text}\n\n</div>\n`;
}

rmSync(OUT_DIR, { recursive: true, force: true });
mkdirSync(OUT_DIR, { recursive: true });

const files = readdirSync(API_DIR).filter((f) => f.endsWith(".md"));
let count = 0;
for (const file of files) {
  const raw = readFileSync(join(API_DIR, file), "utf-8");
  const isIndex = file === "README.md";
  const outName = isIndex
    ? "index.md"
    : `${basename(file, ".md").toLowerCase()}.md`;
  writeFileSync(join(OUT_DIR, outName), transform(raw, { isIndex }));
  count++;
}
console.log(`sync-stdlib: wrote ${count} pages from ${API_DIR}`);
