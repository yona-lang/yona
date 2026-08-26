#!/usr/bin/env sh

set -eu

if ! formatter=$(command -v clang-format 2>/dev/null); then
	echo "error: clang-format is required; install it with your LLVM development tools" >&2
	exit 127
fi

echo "Formatting source and header files"
"$formatter" -i include/*.h src/*.cpp test/*.cpp cli/*.cpp
echo "Done"
