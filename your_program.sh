#!/bin/sh
set -eu
cd "$(dirname "$0")"
cmake -B build -S . >&2
cmake --build build >&2
exec ./build/dns-server "$@"
