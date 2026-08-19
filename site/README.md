# Yona 2.0 documentation site

Public documentation for Yona 2.0, built with
[Astro Starlight](https://starlight.astro.build/) and deployed to
**Cloudflare Pages** from this repository.

The legacy GraalVM-era site stays on GitHub Pages at
[yona-lang.github.io](https://yona-lang.github.io/). Do not replace that
repository.

## Local preview

From the **repository root**:

```bash
python3 scripts/gendocs.py   # refresh docs/api/ from lib/Std ## comments
cd site
pnpm install
pnpm dev                     # http://localhost:4321/
```

Production-like:

```bash
cd site
pnpm build
pnpm preview
```

`pnpm sync` (also run by `dev` / `build`) copies `docs/api/` into
`src/content/docs/stdlib/` and writes `generated/llms/llms.txt`,
`llms-full.txt`, and `llms-small.txt`, served at `/llms.txt` and friends.
Those files are build artifacts — do not edit them by hand.

## Layout

- `src/content/docs/` — handwritten Learn, Guides, Reference, Agents
- `src/styles/theme.css` — Vellum & Ember theme
- `grammars/yona.tmLanguage.json` — Yona TextMate grammar
- `scripts/sync-stdlib.mjs` — stdlib importer
- `scripts/generate-llms.mjs` — `/llms.txt` corpora for agents
- `public/_redirects` — legacy MkDocs URL mappings

## Cloudflare Pages

CI: `.github/workflows/docs-site.yml`.

1. In Cloudflare, create a Pages project named `yona-lang` (or set
   `CLOUDFLARE_PAGES_PROJECT`).
2. Add repository credentials:
   - Secret `CLOUDFLARE_API_TOKEN` — **Account / Cloudflare Pages / Edit**
   - Variable or secret `CLOUDFLARE_ACCOUNT_ID` — from the dashboard URL
   - Optional variable `CLOUDFLARE_PAGES_PROJECT` (default `yona-lang`)
3. Point `yona-lang.org` at the Pages project. Leave
   `yona-lang.github.io` on GitHub Pages for Yona 1.x.

Pushes to `master` / `main` deploy production. Pull requests deploy a
preview when the secrets are present.

## Analytics

Cookieless Plausible Community Edition, same host as other kiket sites
(`plausible.kiket.dev`). Register a site with domain **`yona-lang.org`**
there. Localhost and `*.pages.dev` previews are not recorded (`data-domain`
must match). View-transition navigations send a second pageview via
`astro:after-swap`.
