#!/usr/bin/env bash
# headless_run.sh: run the game on a private Xvfb display, drive it with keys, capture and tally.
#
# usage: headless_run.sh <tag> <seconds> [time:key ...]
#   time:key pairs are seconds after launch, e.g. 45:Return 60:Return 75:space
#
# env: RUNS=N          repeat N times and print a crash total
#      GDB=1           run under gdb, backtrace the first crash
#      PIN=2-5         taskset CPU list
#      EXTRA="--a=b"   extra app flags
#      DISPLAY_NUM, BUILD_DIR, RUN_DIR, OUT_DIR, CONF
#      optional pre_run_hook / post_run_hook functions in headless.conf run around each run
#      ALLOW_USER_GAME=1  run even though a copy of the game outside a headless run dir is running
#
# Project settings live in headless.conf next to this script (see docs/headless.md §3).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
CONF="${CONF:-$HERE/headless.conf}"
[ -f "$CONF" ] || { echo "missing $CONF (run the setup interview in the headless-tester AGENTS.md)"; exit 1; }
# shellcheck source=/dev/null
. "$CONF"

TAG="${1:?usage: headless_run.sh <tag> <seconds> [time:key ...]}"; DUR="${2:?seconds}"; shift 2
KEYS=("$@"); [ ${#KEYS[@]} -eq 0 ] && KEYS=("${DEFAULT_KEYS[@]}")
BUILD_DIR="${BUILD_DIR:-$BUILD_DIR_DEFAULT}"
DISPLAY_NUM="${DISPLAY_NUM:-$DISPLAY_NUM_DEFAULT}"
RUN_DIR="${RUN_DIR:-/tmp/$GAME-headless-$USER-$DISPLAY_NUM}"
OUT_DIR="${OUT_DIR:-$RUN_DIR/out}"; RUNS="${RUNS:-1}"; EXTRA="${EXTRA:-}"
X=":$DISPLAY_NUM"

# --- preflight -------------------------------------------------------------------------------
need=(Xvfb xdotool import); [ "${GDB:-0}" = 1 ] && need+=(gdb); [ -n "${PIN:-}" ] && need+=(taskset)
for t in "${need[@]}"; do command -v "$t" >/dev/null || { echo "missing tool: $t"; exit 1; }; done
[ -x "$BUILD_DIR/$GAME" ] || { echo "no executable at $BUILD_DIR/$GAME"; exit 1; }
if [ -e "/tmp/.X11-unix/X$DISPLAY_NUM" ] || [ -e "/tmp/.X$DISPLAY_NUM-lock" ]; then
  echo "display $X is in use (another agent?). Pick another DISPLAY_NUM."; exit 1
fi

# Rule 3/4: never interfere with a game we didn't start. Headless copies run from /tmp/$GAME-headless-*.
headless=0; user=0
for p in $(pgrep -x "${GAME:0:15}"); do
  case "$(readlink "/proc/$p/cwd" 2>/dev/null)" in
    /tmp/"$GAME"-headless-*) headless=$((headless + 1)) ;;
    *) user=$((user + 1)) ;;
  esac
done
if [ "$user" -gt 0 ] && [ "${ALLOW_USER_GAME:-0}" != 1 ]; then
  echo "the user is running $GAME (not a headless copy). Don't test during a playtest; ask first."
  echo "(set ALLOW_USER_GAME=1 only if the user said it's fine)"; exit 1
fi
if [ "$headless" -ge "${MAX_GAMES:-2}" ]; then
  echo "$headless headless games already running (limit ${MAX_GAMES:-2}). Wait for one to finish."; exit 1
fi

# --- isolated run directory ------------------------------------------------------------------
mkdir -p "$RUN_DIR" "$OUT_DIR"
for f in "${COPY_FILES[@]}"; do
  # shellcheck disable=SC2086  # globs in COPY_FILES are intended
  cp -f "$BUILD_DIR"/$f "$RUN_DIR/" 2>/dev/null
done
for link in "${ASSET_LINKS[@]}"; do
  name="${link%%=*}"; target="${link#*=}"
  [ -e "$RUN_DIR/$name" ] || ln -s "$target" "$RUN_DIR/$name"
done
[ -n "${RUN_CONFIG_NAME:-}" ] && printf '%s\n' "$RUN_CONFIG_CONTENT" > "$RUN_DIR/$RUN_CONFIG_NAME"

{
  echo "set pagination off"; echo "set confirm off"
  for s in "${GDB_PASS_SIGNALS[@]}"; do echo "handle $s nostop noprint pass"; done
  if [ -n "${GDB_BREAK:-}" ]; then
    printf 'break %s\ncommands\nbt 60\nthread apply all bt 25\nkill\nquit\nend\n' "$GDB_BREAK"
  fi
  printf 'run\nbt 60\nthread apply all bt 25\nkill\nquit\n'
} > "$RUN_DIR/crash.gdb"

ENVV=(-u WAYLAND_DISPLAY DISPLAY="$X" SDL_VIDEODRIVER=x11 SDL_JOYSTICK_HIDAPI=0)
[ "${GRAPHICS:-vulkan}" = vulkan ] && ENVV+=(MESA_VK_WSI_DEBUG=sw)
[ -n "${PAD_IGNORE:-}" ] && ENVV+=(SDL_GAMECONTROLLER_IGNORE_DEVICES="$PAD_IGNORE")
ENVV+=("${EXTRA_ENV[@]}")

XPID=; GPID=
cleanup() {
  if [ -n "$GPID" ]; then
    pkill -P "$GPID" 2>/dev/null; kill "$GPID" 2>/dev/null; sleep 1
    pkill -9 -P "$GPID" 2>/dev/null; kill -9 "$GPID" 2>/dev/null; GPID=
  fi
  if [ -n "$XPID" ]; then kill "$XPID" 2>/dev/null; wait "$XPID" 2>/dev/null; XPID=; fi
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

shot() { DISPLAY="$X" import -window root -resize "${SHOT_SIZE:-640x360}" "$1" 2>/dev/null; }

# --- runs ------------------------------------------------------------------------------------
crashes=0
for run in $(seq 1 "$RUNS"); do
  id="${TAG}_$run"
  declare -F pre_run_hook >/dev/null && pre_run_hook
  Xvfb "$X" -screen 0 "${SCREEN:-1920x1080x24}" -nolisten tcp >/dev/null 2>&1 & XPID=$!
  for _ in $(seq 1 50); do [ -e "/tmp/.X11-unix/X$DISPLAY_NUM" ] && break; sleep 0.1; done

  LAUNCH=("./$GAME"); [ "${GDB:-0}" = 1 ] && LAUNCH=(gdb -q -batch -x crash.gdb --args "./$GAME")
  [ -n "${PIN:-}" ] && LAUNCH=(taskset -c "$PIN" "${LAUNCH[@]}")
  # shellcheck disable=SC2086  # EXTRA is a flag list
  ( cd "$RUN_DIR" && { [ -z "${LOG_GLOB:-}" ] || rm -f $LOG_GLOB; } &&
      exec env "${ENVV[@]}" "${LAUNCH[@]}" "${SAFETY_FLAGS[@]}" $EXTRA ) > "$OUT_DIR/$id.stdout.txt" 2>&1 &
  GPID=$!; START=$(date +%s)

  for k in "${KEYS[@]}"; do
    t=${k%%:*}; key=${k#*:}
    [ "$t" -ge "$DUR" ] && break
    while [ $(( $(date +%s) - START )) -lt "$t" ]; do sleep 0.2; done
    kill -0 "$GPID" 2>/dev/null || break
    shot "$OUT_DIR/${id}_${t}.png"
    W=$(DISPLAY="$X" xdotool search --onlyvisible --name . 2>/dev/null | tail -1); [ -z "$W" ] && continue
    DISPLAY="$X" xdotool windowfocus --sync "$W" keydown --window "$W" "$key" 2>/dev/null
    sleep 0.15; DISPLAY="$X" xdotool keyup --window "$W" "$key" 2>/dev/null
  done
  while [ $(( $(date +%s) - START )) -lt "$DUR" ] && kill -0 "$GPID" 2>/dev/null; do sleep 0.5; done
  alive=$(kill -0 "$GPID" 2>/dev/null && echo yes || echo exited)
  shot "$OUT_DIR/${id}_end.png"
  cleanup
  declare -F post_run_hook >/dev/null && post_run_hook

  log="$OUT_DIR/$id.log"
  if [ -n "${LOG_GLOB:-}" ]; then
    # shellcheck disable=SC2086
    (cd "$RUN_DIR" && cat $LOG_GLOB) > "$log" 2>/dev/null
  else
    cp "$OUT_DIR/$id.stdout.txt" "$log"
  fi
  bad=$(cat "$log" "$OUT_DIR/$id.stdout.txt" 2>/dev/null | grep -cE "$CRASH_RE"); bad=${bad:-0}
  pads=0; [ -n "${PAD_RE:-}" ] && pads=$(cat "$log" "$OUT_DIR/$id.stdout.txt" 2>/dev/null | grep -cE "$PAD_RE")
  pads=${pads:-0}
  [ "$bad" -gt 0 ] && crashes=$((crashes + 1))
  echo "$id: crash-lines=$bad real-pads-opened=$pads game-at-end=$alive -> $([ "$bad" -gt 0 ] && echo CRASH || echo ok)"
  [ "$pads" -gt 0 ] && echo "WARNING: a real controller was opened. Stop and tell the user."
done
echo "$TAG: $crashes/$RUNS runs crashed. Output in $OUT_DIR"
