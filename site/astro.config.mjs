// @ts-check
import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";
import { readFileSync } from "node:fs";

const yonaGrammar = JSON.parse(
  readFileSync(
    new URL("./grammars/yona.tmLanguage.json", import.meta.url),
    "utf-8",
  ),
);

export default defineConfig({
  site: "https://yona-lang.org",
  integrations: [
    starlight({
      title: "Yona",
      description:
        "A statically typed functional language that compiles to native code through LLVM.",
      social: [
        {
          icon: "github",
          label: "GitHub",
          href: "https://github.com/yona-lang/yona",
        },
      ],
      customCss: [
        "@fontsource/source-sans-3/400.css",
        "@fontsource/source-sans-3/600.css",
        "@fontsource/ibm-plex-mono/400.css",
        "@fontsource/ibm-plex-mono/500.css",
        "./src/styles/theme.css",
      ],
      expressiveCode: {
        shiki: {
          langs: [yonaGrammar],
          langAlias: { yonai: "yona", Yona: "yona" },
        },
        styleOverrides: {
          borderRadius: "0.35rem",
          codeFontFamily: "'IBM Plex Mono', ui-monospace, monospace",
        },
      },
      sidebar: [
        {
          label: "Start",
          items: [
            { label: "Why Yona", slug: "why-yona" },
            { label: "Installation", slug: "install" },
            { label: "Quick start", slug: "learn/quick-start" },
            { label: "CMake integration", slug: "reference/cmake" },
          ],
        },
        {
          label: "Learn",
          items: [
            { label: "Syntax and evaluation", slug: "learn/syntax" },
            { label: "Functions", slug: "learn/functions" },
            { label: "Pattern matching", slug: "learn/pattern-matching" },
            { label: "Types and data", slug: "learn/types" },
            { label: "Collections", slug: "learn/collections" },
            { label: "Concurrency", slug: "learn/concurrency" },
            { label: "Effects", slug: "learn/effects" },
            { label: "Modules", slug: "learn/modules" },
            { label: "Style", slug: "learn/style" },
          ],
        },
        {
          label: "Guides",
          items: [
            { label: "Concurrency in depth", slug: "guides/concurrency" },
            { label: "The type system", slug: "guides/type-system" },
            { label: "Memory and linearity", slug: "guides/memory" },
            {
              label: "Persistent data structures",
              slug: "guides/persistent-data-structures",
            },
            { label: "Traits", slug: "guides/traits" },
            {
              label: "Modules and interfaces",
              slug: "guides/modules-interfaces",
            },
            { label: "Iterators and streams", slug: "guides/iterators" },
            { label: "Accelerators (GPU)", slug: "guides/accelerators" },
            { label: "Performance", slug: "guides/performance" },
            { label: "Editor and language server", slug: "guides/editor" },
          ],
        },
        {
          label: "Reference",
          items: [
            {
              label: "Language specification",
              slug: "reference/specification",
            },
            { label: "Prelude", slug: "reference/prelude" },
            { label: "Compiler CLI", slug: "reference/cli" },
            { label: "CMake integration", slug: "reference/cmake" },
            { label: "Error codes", slug: "reference/error-codes" },
          ],
        },
        {
          label: "Standard library",
          collapsed: true,
          items: [{ autogenerate: { directory: "stdlib" } }],
        },
        {
          label: "For AI agents",
          items: [{ label: "Agent guide", slug: "agents" }],
        },
      ],
    }),
  ],
});
