#!/bin/sh
# Builds the standalone release directory for pengubirds.
# Runtime libraries and symbol packs are taken from this checkout.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-"$ROOT/out/release/pengubirds-1.0-linux-x86_64"}
BUILD="$ROOT/out/build/release"
SWARC_REPO="https://github.com/JTM-rootstorm/SwARc.git"
SWARC_COMMIT="6b25b3cefee425eb24a6fd0d096e6757c6cf9323"
SWARC_DIR="$ROOT/out/deps/SwARc"

# SwARc is a build-time dependency, not vendored. Fetch the exact base
# revision and apply the compatibility patch set once.
if [ ! -d "$SWARC_DIR/.git" ]; then
  mkdir -p "$ROOT/out/deps"
  git clone "$SWARC_REPO" "$SWARC_DIR"
fi
if [ "$(git -C "$SWARC_DIR" rev-parse HEAD 2>/dev/null || true)" != "$SWARC_COMMIT" ]; then
  git -C "$SWARC_DIR" fetch origin "$SWARC_COMMIT" 2>/dev/null || git -C "$SWARC_DIR" fetch origin
  git -C "$SWARC_DIR" checkout --force "$SWARC_COMMIT"
  git -C "$SWARC_DIR" clean -fdx
fi
if [ ! -f "$SWARC_DIR/.pengubirds-patched" ]; then
  git -C "$SWARC_DIR" apply "$ROOT/patches/swarc-local.patch"
  touch "$SWARC_DIR/.pengubirds-patched"
fi

if [ ! -f "$BUILD/build.ninja" ]; then
  cmake --preset release -DRECOMP_LLVM_ROOT="${RECOMP_LLVM_ROOT:-/usr}" \
    -DSWARC_DIR="$SWARC_DIR"
fi
cmake --build --preset release --target pengubirds recomp_lift recomp_win32 \
  recomp_opengl recomp_dsound recomp_xinput recomp_base recomp_x86 recomp_x87 \
  recomp_pe recomp_gfx recomp_hlsl recomp_audio recomp_log >/dev/null

rm -rf "$OUT"
mkdir -p "$OUT/payload/bin" "$OUT/payload/lib" "$OUT/payload/include" \
         "$OUT/payload/src" "$OUT/payload/symbols" "$OUT/payload/manifests"

cp "$BUILD/pengubirds" "$OUT/pengubirds"
cp "$BUILD/swarc/tools/lifter_x86/recomp_lift" "$OUT/payload/bin/recomp_lift.bin"
cat > "$OUT/payload/bin/recomp_lift" <<'WRAP'
#!/bin/sh
# The lifter is built against this checkout; make its runtime portable.
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LD_LIBRARY_PATH="$DIR/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
exec "$DIR/recomp_lift.bin" "$@"
WRAP
chmod +x "$OUT/payload/bin/recomp_lift"
cp "$BUILD"/swarc/librecomp_*.so "$OUT/payload/lib/"
cp "$BUILD"/swarc/platforms/win32/librecomp_*.so "$OUT/payload/lib/"
cp "$ROOT"/src/*.cpp "$OUT/payload/src/"
cp "$ROOT"/src/*.h "$OUT/payload/src/" 2>/dev/null || true
cp -a "$ROOT"/symbols/. "$OUT/payload/symbols/"
cp -a "$ROOT"/manifest/. "$OUT/payload/manifests/"
cp "$ROOT"/README.md "$OUT/README.md"
cp "$ROOT"/LICENSE "$OUT/LICENSE"

(cd "$SWARC_DIR/src" && find . -type f \( -name '*.h' -o -name '*.hpp' \) \
    -exec cp --parents {} "$OUT/payload/include/" \;)
(cd "$SWARC_DIR/platforms/win32/src" && find . -type f \( -name '*.h' -o -name '*.hpp' \) \
    -exec cp --parents {} "$OUT/payload/include/" \;)

echo "release: $OUT"
du -sh "$OUT"
