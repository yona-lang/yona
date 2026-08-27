#!/usr/bin/env sh

cloc --exclude-list-file=.gitignore --exclude-dir=out,lib,node_modules,.cache .
