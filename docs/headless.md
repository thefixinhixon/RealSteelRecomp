# Headless testing for game ports on a shared Linux desktop

How agents test a game (or any graphical app) on a machine the user is using at the same time: no
windows on the user's monitors, no sound, no real controllers, no interference with the user's own
build or settings. It is written for Linux ports (Vulkan or OpenGL, SDL input/audio). It worked
across a full recompilation port (Splatterhouse X360 → Linux): hundreds of runs, crash-rate
statistics, performance A/B tests and profiling, all while the user kept working.

---

## 1. Hard rules

1. **Never test on the user's screens or speakers.** Every run goes through the headless harness
   (Xvfb + software presentation + muted audio). Never take desktop screenshots; capture the virtual
   display only.
2. **Never open the user's real controller.** A killed test once left a pad rumbling nonstop.
   Disable real input devices on the **command line** (config-file values may not be honoured),
   drive the game with a keyboard-emulated pad, and check the log after every run for a
   "controller added" line.
3. **Never build into or kill a game you didn't start.** The user's play directory and its config
   file belong to them. Rebuilding replaces shared libraries under a running game and corrupts it.
   Check `pgrep -a <game>` before building or killing anything, and pick processes by working
   directory (see §6), never with `pgrep -n`.
4. **Don't run tests while the user is playtesting.** Headless runs share CPU and GPU and ruin the
   user's performance numbers. Tell the user when a batch starts and when it ends; if a long batch
   is queued, let them choose the order.
5. **Don't commit or push unless the user says so.** Agent branches are committed locally; the user
   approves merges.

## 2. The virtual display

| Need | How |
|---|---|
| Display | `Xvfb :<N> -screen 0 1920x1080x24 -nolisten tcp &` (one display number per agent) |
| Force X11, not the user's Wayland session | `env -u WAYLAND_DISPLAY DISPLAY=:<N> SDL_VIDEODRIVER=x11` |
| Vulkan on Xvfb (no DRI3) | `MESA_VK_WSI_DEBUG=sw`: Mesa presents through a CPU copy; rendering still runs on the real GPU |
| OpenGL on Xvfb | works with llvmpipe by default (slow); with GLX on the GPU it depends on the driver |
| Screenshot | `DISPLAY=:<N> import -window root -resize 640x360 out.png` (ImageMagick) |
| Keys | `xdotool search --onlyvisible --name .` to find the window, then `windowfocus --sync`, `keydown --window W key`, `sleep 0.15`, `keyup` (a bare `key` is often too short for games that poll input once per frame) |

**Audio:** use the game's mute option (`--audio_mute` or similar). SDL's `dummy` audio driver isn't
always compiled in, so don't rely on `SDL_AUDIODRIVER=dummy` without checking.

**Input safety:**
- Disable the input backend on the command line (e.g. `--input_backend=none`).
- Set `SDL_JOYSTICK_HIDAPI=0`, plus `SDL_GAMECONTROLLER_IGNORE_DEVICES=0xVVVV/0xPPPP` for the user's
  pad (find it with `lsusb`).
- Turn on the app's keyboard-as-pad mode in the run directory's config.
- After every run, grep the log for the device-added message. The harness prints a warning if it
  finds one.
- If a pad does get stuck rumbling, briefly launching the game normally usually sends "motors off"
  when it opens the pad.

**Headless distorts performance.** With `MESA_VK_WSI_DEBUG=sw`, `vkQueuePresentKHR` waits for the
GPU and copies the image on the CPU while holding the queue. That cost about 5 ms per frame here. Use
headless numbers for A/B comparisons on the same machine, never as absolute fps. A real-display
number has to come from the user (see §8).

## 3. The harness script

One script, in the repo (`tools/linux/headless_run.sh`), that every agent uses. It must:

- **Run a copy:** copy the executable and its `.so` files into an isolated run directory
  (`/tmp/<game>-headless-$USER-<DISPLAY_NUM>`), symlink the game assets there, and write a minimal
  config there (log file name, keyboard-pad mode). It never touches the build folder or the user's
  config.
- **Follow a key script:** `time:key` pairs relative to launch, e.g.
  `45:Return 60:Return 75:space 90:space`.
- **Capture:** take a screenshot at every key press and one at the end
  (`<tag>_<n>_<t>.png`, `<tag>_<n>_end.png`). Save the full log (`<tag>_<n>.log`), with log rotation
  turned off or set very large so a crash flood doesn't push the start of the problem out, and the
  stdout/gdb output (`<tag>_<n>.stdout.txt`).
- **Repeat and tally:** `RUNS=N` repeats the run and prints one line per run plus a crash total
  (count the project's crash signatures in the log, e.g. unhandled access violations, failed allocs,
  calls to unregistered functions).
- **Debug on demand:** `GDB=1` runs under `gdb -batch` with a breakpoint on the first crash
  signature, prints that backtrace plus `thread apply all bt`, then kills the game. Pass through
  the signals the runtime uses (`handle SIGSEGV nostop noprint pass`, plus SIGBUS, SIGUSR1/2 and
  SIGPIPE).
- **Options:** `EXTRA="--flag=value"` for app flags, `PIN=2` for `taskset`, `DISPLAY_NUM`,
  `BUILD_DIR`, `RUN_DIR`, `OUT_DIR`.
- **Clean up:** kill the process tree (`pkill -P`, then `kill -9`) and Xvfb, even when the game hangs.
- **Check its tools:** fail early if `Xvfb`, `xdotool`, `import` or `gdb` is missing.

Project settings (executable, safety flags, crash signatures, default keys) live in a sourced
`headless.conf` next to the script, so the script itself is the same in every project. The full
version is [`tools/linux/headless_run.sh`](../tools/linux/headless_run.sh); a shorter skeleton is in §10.

## 4. Checkpoints

Define them once, run them headless, and record the date, commit and numbers on the work board.

| ID | What | Typical form |
|---|---|---|
| C0 | Boots | 1 run reaches the main menu, 0 crash signatures, no real pad opened |
| C1 | Stable where it used to crash | `RUNS=10 GDB=1`, default settings **and** the settings that reproduce the crash most often; 0 crashes, and every `_end.png` shows the expected scene |
| C2 | Soak | one 15-minute run that keeps playing, then every level (needs a level-warp/debug-load flag) |
| C3 | Performance | fps in a fixed heavy scene, compared with the previous checkpoint |
| C4 | User sign-off | the user plays a session with their own controller |

A fix for an intermittent bug isn't done until C1 passes with a measured "before" rate, such as
5/10 crashes without the fix against 0/10 with it.

## 5. Reading results

- Look at the PNGs with an image viewer or the agent's image-reading tool. A 640x360 PNG per key
  press tells you whether the script is in sync with the game (title, menu, loading, gameplay).
- **Boot time varies:** a fresh worktree or an empty shader cache boots slower, so presses get lost
  on the title screen. Use a slow-boot script with later, repeated presses when in doubt.
- **ImageMagick gotcha:** `fx:mean.g` returns 0 on greyscale PNGs. Use `identify -verbose` for
  channel statistics.
- **Comparing images:** for "is the picture the same with the fix on and off", take full-resolution
  captures at the same timestamps in both runs and compare crops side by side
  (`convert a.png b.png +append cmp.png`).

## 6. Profiling without root

`perf` is often blocked (`perf_event_paranoid` ≥ 3), and `ptrace_scope=1` only lets a parent attach.

1. Build a one-function preload library:
   ```c
   // ptracer.c: gcc -shared -fPIC -o ptracer.so ptracer.c
   #include <sys/prctl.h>
   __attribute__((constructor)) static void allow(void) { prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY); }
   ```
2. Start the test with `LD_PRELOAD=/path/ptracer.so tools/linux/headless_run.sh ...`.
3. Sample all threads with `eu-stack -p <pid>` about once a second (symbolized), for 30–60 samples
   in the scene you care about. Count the top frames per thread with a small script.
4. **Pick the PID by run directory:** `readlink /proc/<pid>/cwd` must equal your own run directory.
   Another agent's game may be newer; `pgrep -n` once nearly killed someone else's run.

Heavy instrumentation changes timing: race bugs can disappear, and fine-grained timers once showed
10–16 fps for a scene that ran at 28 fps. Keep counters cheap, behind a flag, and log them
aggregated every few seconds.

## 7. Measuring performance

- **Add a periodic perf log behind a flag** (one line every 2 s): fps, frame time of the critical
  thread, draws, passes, uploads, and timers for the phases you suspect. Give a new suspect its own
  log line instead of guessing.
- **A/B on the same build:** put each change behind a runtime flag (default to the new behaviour
  only after it's verified) and run both arms with the same key script, scene and capture
  timestamps. Compare the perf lines from the heavy-scene intervals (e.g. intervals with 800+ draws
  per frame) and check that the screenshots match.
- **Ruled out:** record every experiment that didn't help, with its numbers, in a "Ruled out" list,
  so nobody repeats it.

## 8. User playtests

- Give the user a **test copy** of the build in its own folder, with a copy of their config, and
  leave their play build alone until they approve a merge.
- Give them one command to run, e.g. `cd <test copy> && ./game --perf_log=true`.
- Make sure no headless run is active first (rule 4).
- **Compare sessions:** after each session, rename the log (`game.run3.log`) and compare by fps band:
  share of time under 30, 30–45, 45–55 and 55+, median, 10th percentile, and the phase timers per
  band. The bad band shows where the time goes.
- **Ask about what the log can't show:** stutter, pop-in, soft-locks, input feel.

## 9. Several agents at once

- **Separate copies:** one git worktree, one build directory and one `DISPLAY_NUM` (and so one run
  directory) per agent.
- **Run limit:** at most two headless games at a time on a desktop machine.
- **Work board:** a table in `AGENTS.md` (ID, task, owner, notes). An agent edits only its own rows.
- **Shared generated code:** changes that regenerate shared generated code (codegen, shader
  compilers) are serialized and announced on the board first.
- **Unpublished commits:** commits that exist only locally (e.g. an unpublished submodule commit)
  are fetched into worktrees from the main checkout.
- **Handoff:** when an agent stops, it updates its board row and the "Ruled out" list.

## 10. Harness skeleton

```bash
#!/usr/bin/env bash
# usage: headless_run.sh <tag> <seconds> [time:key ...]
set -u
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
TAG="${1:?tag}"; DUR="${2:?seconds}"; shift 2
KEYS=("$@"); [ ${#KEYS[@]} -eq 0 ] && KEYS=(45:Return 60:Return 75:space 90:space)
GAME=mygame                                            # executable name
BUILD_DIR="${BUILD_DIR:-$REPO/out/build/release}"
DISPLAY_NUM="${DISPLAY_NUM:-91}"
RUN_DIR="${RUN_DIR:-/tmp/$GAME-headless-$USER-$DISPLAY_NUM}"
OUT_DIR="${OUT_DIR:-$RUN_DIR/out}"; RUNS="${RUNS:-1}"; EXTRA="${EXTRA:-}"
CRASH_RE='Unhandled access violation|FATAL'            # project crash signatures
PAD_RE='ControllerDeviceAdded'                         # "real pad opened" log line

for t in Xvfb xdotool import gdb; do command -v $t >/dev/null || { echo "missing $t"; exit 1; }; done
mkdir -p "$RUN_DIR" "$OUT_DIR"
cp -f "$BUILD_DIR/$GAME" "$BUILD_DIR"/*.so "$RUN_DIR/" 2>/dev/null
[ -e "$RUN_DIR/assets" ] || ln -s "$REPO/game" "$RUN_DIR/assets"
printf "log_file = 'game.log'\nkeyboard_pad = true\n" > "$RUN_DIR/config.toml"

crashes=0
for run in $(seq 1 "$RUNS"); do
  id="${TAG}_$run"
  Xvfb ":$DISPLAY_NUM" -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!; sleep 1
  LAUNCH=(./$GAME); [ "${GDB:-0}" = 1 ] && LAUNCH=(gdb -q -batch -x crash.gdb --args ./$GAME)
  [ -n "${PIN:-}" ] && LAUNCH=(taskset -c "$PIN" "${LAUNCH[@]}")
  ( cd "$RUN_DIR" && rm -f game*.log && exec env -u WAYLAND_DISPLAY DISPLAY=":$DISPLAY_NUM" \
      SDL_VIDEODRIVER=x11 MESA_VK_WSI_DEBUG=sw SDL_JOYSTICK_HIDAPI=0 \
      "${LAUNCH[@]}" --mute --input_backend=none $EXTRA ) > "$OUT_DIR/$id.stdout.txt" 2>&1 &
  GPID=$!; START=$(date +%s)
  for k in "${KEYS[@]}"; do
    t=${k%%:*}; key=${k#*:}
    while [ $(( $(date +%s) - START )) -lt "$t" ]; do sleep 0.2; done
    kill -0 $GPID 2>/dev/null || break
    DISPLAY=":$DISPLAY_NUM" import -window root -resize 640x360 "$OUT_DIR/${id}_${t}.png" 2>/dev/null
    W=$(DISPLAY=":$DISPLAY_NUM" xdotool search --onlyvisible --name . 2>/dev/null | tail -1); [ -z "$W" ] && continue
    DISPLAY=":$DISPLAY_NUM" xdotool windowfocus --sync "$W" keydown --window "$W" "$key" 2>/dev/null
    sleep 0.15; DISPLAY=":$DISPLAY_NUM" xdotool keyup --window "$W" "$key" 2>/dev/null
  done
  while [ $(( $(date +%s) - START )) -lt "$DUR" ] && kill -0 $GPID 2>/dev/null; do sleep 0.5; done
  DISPLAY=":$DISPLAY_NUM" import -window root -resize 640x360 "$OUT_DIR/${id}_end.png" 2>/dev/null
  pkill -P $GPID; kill $GPID; sleep 1; pkill -9 -P $GPID; kill -9 $GPID; kill $XPID; wait $XPID
  cat "$RUN_DIR"/game*.log > "$OUT_DIR/$id.log" 2>/dev/null
  bad=$(grep -cE "$CRASH_RE" "$OUT_DIR/$id.log"); pads=$(grep -cE "$PAD_RE" "$OUT_DIR/$id.log")
  [ "$bad" -gt 0 ] && crashes=$((crashes + 1))
  echo "$id: crash-lines=$bad real-pads-opened=$pads -> $([ "$bad" -gt 0 ] && echo CRASH || echo ok)"
  [ "$pads" -gt 0 ] && echo "WARNING: a real controller was opened"
done
echo "$TAG: $crashes/$RUNS runs crashed. Output in $OUT_DIR"
```

The `crash.gdb` file sets `pagination off`, passes the runtime's signals, puts a breakpoint on the
crash-reporting function, and in its `commands` block prints `bt 60`, then
`thread apply all bt 25`, then `kill` and `quit`.

## 11. Gotchas collected so far

- **Leaked shared memory:** runtimes that back guest memory with a big `/dev/shm` file leak it
  when a run is killed, unless the file is unlinked right after `shm_open`. A full `/dev/shm` then
  shows up as SIGBUS much later (e.g. on a level load).
- **Command line vs config:** some settings are only honoured on the command line (input backend
  here). Always pass safety settings as flags.
- **Pacing:** some games wait for vblank, so fps snaps between 60 and 30. Report frame *work* time
  next to fps, or a 2 ms gain can look like nothing.
- **Fake vblank:** with vsync off, an emulated or virtual vblank can run at 1 kHz and make game
  time run fast. Check game speed, not just fps.
- **Cold caches:** use a temporary shader/pipeline cache directory (and `MESA_SHADER_CACHE_DIR`)
  for cold-cache tests, so the user's cache stays untouched.

---

## Project specifics (Real Steel)

- **Executable, run command, asset layout:** `realsteel` + `librexruntime.so` + `librexgpu-xenos.so`
  from `out/build/linux-amd64-release` (always a matched set; the build copies the libraries next to
  the exe). Game data is symlinked from `/mnt/sdb1/Games/RealSteel/run/gamedata` (override with
  `GAMEDATA=`). Saves and caches stay in the run dir (`--user_data_root=userdata --cache_root=cache`).
- **Crash signatures:** `[critical]`, `[FATAL]`, `Unhandled access violation`, `unregistered function`.
  The 478 `PhysicalHeap::Alloc` errors at boot are the game probing for its largest memory pool -
  harmless, not a crash.
- **Real controller opened:** log line `OnControllerDeviceAdded`. The user's pad is a PowerA Xbox
  Series X wired controller (`20d6:2062`). `--input_backend=xinput` creates no SDL gamepad driver at
  all on Linux, so only the keyboard pad (`--mnk_mode=true`) is live.
- **Keys (keyboard pad):** Return = Start, Space = A, Backspace = B, `a`/`d`/`w`/`s` = left stick,
  arrows = right stick, Shift+arrows = d-pad. `comma` is unbound: use it as a "screenshot only" key.
- **Boot to fight (fresh save, ~150 s):**
  `40:Return 52:a 55:space`, then `space` every 4 s to the Robot Select screen (~99 s), `102:a 104:space`
  buys the robot, `108:space`, six `s` + `space` opens PROFILE, six `s` + `space` = ACCEPT, then `space`
  every 5 s: the first career match runs ~170-250 s. Delete `$RUN_DIR/userdata` first for a fresh save.
- **Perf log:** `--rs_perf_log=true` (RSPERF line every 2 s). Heavy scene for C3: the new-career arena
  intro (log windows 26-46 of an uncapped run), and the first fight.
- **Profiling:** `perf_event_paranoid=4`, `ptrace_scope=1`: build `tools/linux/ptracer.so` and sample
  with `eu-stack` (guide §6).
- **Display numbers in use by agents:** `:91` (default).
- **Gotcha:** the runtime leaks a 4.8 GB `/dev/shm/xenia_memory_*` file per killed run; the harness'
  `post_run_hook` removes the ones its own run created.
