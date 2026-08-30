#!/usr/bin/env node
/**
 * Write public/llms.txt, llms-full.txt, and llms-small.txt from the
 * Starlight content tree so the files exist in `astro dev` and in the
 * static build (not only as prerendered plugin routes).
 */
import { mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const SITE_ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const DOCS = join(SITE_ROOT, "src", "content", "docs");
const OUT_DIR = join(SITE_ROOT, "generated", "llms");
const ORIGIN = "https://yona-lang.org";

const DESCRIPTION =
  "Documentation for the Yona programming language: a statically typed, LLVM-compiled functional language with transparent concurrency, algebraic effects, and linear resources.";
const DETAILS =
  "Yona compiles to native executables via yonac. Errors carry stable codes (yonac --explain E0xxx). Function arrows carry effect rows. Docs cover the language specification, guides, CLI reference, and standard library.";

function walk(dir, acc = []) {
  for (const ent of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, ent.name);
    if (ent.isDirectory()) walk(path, acc);
    else if (/\.(md|mdx)$/.test(ent.name)) acc.push(path);
  }
  return acc;
}

function parseDoc(raw) {
  const m = raw.match(/^---\r?\n([\s\S]*?)\r?\n---\r?\n([\s\S]*)$/);
  const fm = m ? m[1] : "";
  const body = m ? m[2] : raw;
  const title =
    (fm.match(/^title:\s*(.+)$/m) ?? [])[1]
      ?.trim()
      .replace(/^['"]|['"]$/g, "") ?? "";
  const description = (fm.match(/^description:\s*(.+)$/m) ?? [])[1]
    ?.trim()
    .replace(/^['"]|['"]$/g, "");
  return { title, description: description ?? "", body: body.trim() };
}

function urlFor(file) {
  let rel = relative(DOCS, file)
    .replace(/\\/g, "/")
    .replace(/\.(md|mdx)$/, "");
  if (rel === "index" || rel.endsWith("/index"))
    rel = rel.replace(/\/?index$/, "");
  return rel ? `${ORIGIN}/${rel}/` : `${ORIGIN}/`;
}

function pageSection(p) {
  const desc = p.description ? `: ${p.description}` : "";
  return `- [${p.title}](${p.url})${desc}`;
}

function dumpPage(p) {
  return `# ${p.title}\n\nSource: ${p.url}\n\n${p.body}\n`;
}

const pages = walk(DOCS)
  .map((file) => {
    const parsed = parseDoc(readFileSync(file, "utf-8"));
    return { ...parsed, url: urlFor(file), file };
  })
  .filter((p) => p.title)
  .sort((a, b) => a.url.localeCompare(b.url));

const core = pages.filter((p) => !p.url.includes("/stdlib/"));
const stdlib = pages.filter((p) => p.url.includes("/stdlib/"));

const index = [
  "# Yona",
  "",
  `> ${DESCRIPTION}`,
  "",
  DETAILS,
  "",
  "## Documentation sets",
  "",
  `- [Abridged documentation](${ORIGIN}/llms-small.txt): Learn, Guides, Reference, and Agents (no stdlib API pages)`,
  `- [Complete documentation](${ORIGIN}/llms-full.txt): every page including the standard library reference`,
  "",
  "## Pages",
  "",
  ...core.map(pageSection),
  "",
  "## Standard library",
  "",
  ...stdlib.map(pageSection),
  "",
].join("\n");

mkdirSync(OUT_DIR, { recursive: true });
writeFileSync(join(OUT_DIR, "llms.txt"), index);
writeFileSync(
  join(OUT_DIR, "llms-small.txt"),
  core.map(dumpPage).join("\n---\n\n"),
);
writeFileSync(
  join(OUT_DIR, "llms-full.txt"),
  pages.map(dumpPage).join("\n---\n\n"),
);

console.log(
  `generate-llms: wrote llms.txt (${pages.length} pages), llms-small.txt (${core.length}), llms-full.txt (${pages.length})`,
);
