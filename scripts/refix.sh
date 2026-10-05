#!/bin/bash
# Re-apply ALL house fixes to a (fresh) codegen tree, in order.
# MUST be re-run after every manual codegen: codegen regenerates
# rexglue.cmake too, silently restoring the live build-graph codegen
# rule, and emits raw sources without the hand fixes.
set -e
cd "$(dirname "$0")/.."
python3 scripts/freeze-codegen.py
python3 scripts/fix-tailcalls.py
touch generated/default/codegen.build.stamp
echo "refix complete"
