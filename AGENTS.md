# Real Steel recomp - agent notes

## Headless testing

Agents test `realsteel` headless while the user works on this machine. Full method:
[`docs/headless.md`](docs/headless.md). Harness: `tools/linux/headless_run.sh`, settings in
`tools/linux/headless.conf`.

### Hard rules

1. Every run goes through `tools/linux/headless_run.sh`. Never launch the game on the user's
   display, never take desktop screenshots, never play sound.
2. Never open the user's controller (PowerA Xbox Series X wired pad, 20d6:2062). The safety flags `--input_backend=xinput --mnk_mode=true --audio_mute=true` are always
   passed on the command line. If a run prints `WARNING: a real controller was opened`, stop and
   tell the user.
3. The user's play build is `/mnt/sdb1/Games/RealSteel/run (and the Factual Metal Launcher install)`. Never build into it, copy over it, edit its
   config, or kill a game running from it. Pick processes by working directory, never `pgrep -n`.
4. No headless runs while the user is playtesting. Tell them when a batch starts and ends:
   a line in chat when a batch starts and when it ends.
5. Commit only when the user asks; never push without asking.

### Day to day

```bash
tools/linux/headless_run.sh boot 120                    # default key script, one run
RUNS=10 GDB=1 tools/linux/headless_run.sh c1 180        # crash rate + backtraces
EXTRA="--vsync=false --frame_limit=0 --rs_perf_log=true" tools/linux/headless_run.sh ab_on 180 40:Return 52:a 55:space 62:space
```

Output goes to `/tmp/realsteel-headless-$USER-<display>/out`. Look at the PNGs, not only the tally.

### Agents

| Agent | Display | Worktree | Build dir |
|---|---|---|---|
| main | :91 | repo/ | out/build/linux-amd64-release |

At most 2 headless games at a time.

### Checkpoints

| ID | What | Last result (date, commit, numbers) |
|---|---|---|
| C0 | Boots to the title screen, 0 crash lines, no real pad | 2026-11-07, pre-commit tree: boots, 0 crash lines, 0 pads |
| C1 | Fresh save to the end of the first career fight, 3 runs | 2026-11-07, branch perf-pacing-overhaul: 3/3 ok, 0 crash lines, 0 pads; fight avg 58.8 / 59.5 / 59.2 fps (vsync on, limit 60, headless) |
| C2 | Soak + every level (no warp flag; use a saved career) | |
| C3 | Perf in career arena intro / first fight (--rs_perf_log=true) | 2026-11-07: arena uncapped 32.9 fps (v1.0) -> 67.7 fps; see README "Performance work" |
| C4 | User sign-off with their own controller | 2026-11-07, v1.1: user played it - "seems to be much better" (no fps numbers yet) |

### Work board

| ID | Task | Owner | Notes |
|---|---|---|---|
| | | | |

An agent edits only its own rows.

### Ruled out

Experiments that didn't help, with their numbers, so nobody repeats them.

- `--vulkan_pipeline_creation_threads`, `--xma_worker`, `--audio_*` (gain/highpass/stereo/queue): not
  runtime flags at all; the old launcher passed them and they did nothing.
- `--perf_log_csv`: declared but never opened by the runtime. Use `--rs_perf_log=true`.
