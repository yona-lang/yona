#!/usr/bin/env sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v python3 >/dev/null 2>&1; then
  python_command=python3
else
  python_command=python
fi

exec "$python_command" "$script_dir/quality.py" "$@"
