#!/usr/bin/env bash
# Copy the canonical TextMate grammar into the VS Code extension.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/site/grammars/yona.tmLanguage.json"
dst="$root/editors/vscode/syntaxes/yona.tmLanguage.json"
if [[ ! -f "$src" ]]; then
  echo "missing canonical grammar: $src" >&2
  exit 1
fi
mkdir -p "$(dirname "$dst")"
cp "$src" "$dst"
echo "synced $src -> $dst"
