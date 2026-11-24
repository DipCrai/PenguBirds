#!/bin/sh
# Removes the fetched SwARc dependency and the build tree. The release
# output under out/release is kept; use `rm -rf out` for a full wipe.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rm -rf "$ROOT/out/deps" "$ROOT/out/build"
echo "removed $ROOT/out/deps and $ROOT/out/build"
