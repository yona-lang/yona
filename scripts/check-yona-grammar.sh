#!/usr/bin/env bash
# Fail if the editor grammar drifts from site/grammars/yona.tmLanguage.json.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/site/grammars/yona.tmLanguage.json"
dst="$root/editors/vscode/syntaxes/yona.tmLanguage.json"
if [[ ! -f "$src" || ! -f "$dst" ]]; then
  echo "grammar files missing" >&2
  exit 1
fi
if ! cmp -s "$src" "$dst"; then
  echo "editors/vscode/syntaxes/yona.tmLanguage.json is out of date." >&2
  echo "Run: ./scripts/sync-yona-grammar.sh" >&2
  diff -u "$src" "$dst" >&2 || true
  exit 1
fi
if grep -q '"daemon"' "$src"; then
  echo "canonical grammar still highlights leftover 1.x keyword daemon" >&2
  exit 1
fi
for tok in fun lambda record deriving native io '@borrow'; do
  if ! grep -q "$tok" "$src"; then
    echo "canonical grammar missing token: $tok" >&2
    exit 1
  fi
done
if ! grep -F -q '[|' "$src" && ! grep -F -q '\\[\\|' "$src"; then
  echo "canonical grammar missing parallel comprehension token [| " >&2
  exit 1
fi
echo "Yona TextMate grammars match"
