# Yona documentation site

The public documentation site for Yona, built with
[Astro Starlight](https://starlight.astro.build/) and deployed to Cloudflare
Pages from this repository.

## Local preview

From the repository root:

```bash
python3 scripts/gendocs.py
cd site
pnpm install --frozen-lockfile
pnpm dev
```

For a production build:

```bash
cd site
pnpm build
pnpm preview
```

`pnpm sync` (also run by `dev` and `build`) copies `docs/api/` into
`src/content/docs/stdlib/` and writes the generated `llms.txt` corpora. Those
files are build artifacts; do not edit them by hand.

## Layout

- `src/content/docs/` contains handwritten Learn, Guides, and Reference pages.
- `src/styles/theme.css` provides the technical slate-and-cobalt visual system.
- `grammars/yona.tmLanguage.json` defines the Yona TextMate grammar.
- `scripts/sync-stdlib.mjs` imports the generated standard-library reference.
- `scripts/generate-llms.mjs` creates the machine-readable documentation views.

## Cloudflare Pages

CI is defined in `.github/workflows/docs-site.yml`.

1. Create a Pages project named `yona-lang`, or set `CLOUDFLARE_PAGES_PROJECT`.
2. Add `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` to repository secrets
   or variables.
3. Point `yona-lang.org` at the Pages project.

Pushes to `master` or `main` deploy production. Pull requests deploy previews
when the deployment credentials are available. The site deliberately includes no
third-party analytics script.
