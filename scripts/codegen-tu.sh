#!/bin/bash
# Codegen + post-fix for the title update build (realsteel-tu).
#
# Needs assets-tu/default.xex (the base game, v0.0.0.5) and
# assets-tu/default.xexp (from title update 2, v0.0.2.5) - codegen applies
# the patch. Writes generated-tu/default. The codegen CLI always rewrites
# generated/rexglue.cmake for whatever manifest it ran; the base tree's
# frozen copy is put back afterwards.
#
# usage: scripts/codegen-tu.sh [path/to/rexglue]
set -e
cd "$(dirname "$0")/.."
REXGLUE="${1:-${REXGLUE:-rexglue}}"
for f in assets-tu/default.xex assets-tu/default.xexp; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done
backup="$(mktemp)"
trap 'cp "$backup" generated/rexglue.cmake; rm -f "$backup"' EXIT
cp generated/rexglue.cmake "$backup"
"$REXGLUE" codegen --ignore-stamp realsteel_tu_manifest.toml
rm -f generated-tu/rexglue.cmake
python3 scripts/fix-tailcalls.py generated-tu/default
touch generated-tu/default/codegen.build.stamp
echo "title update codegen complete"
