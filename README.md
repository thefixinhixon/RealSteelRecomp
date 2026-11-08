# Real Steel — Linux Recompilation

A native Linux port of the Xbox 360 XBLA game **Real Steel** (Yuke's,
2011 — the robot-boxing game based on the DreamWorks film), built with
the [ReXGlue SDK](https://github.com/ReXGlue/rexglue-sdk) static
recompiler — the same toolchain family as
['Splosion Man](https://github.com/thefixinhixon/SplosionManRecomp),
[The Maw](https://github.com/thefixinhixon/TheMawRecomp) and
[Condemned 2](https://github.com/thefixinhixon/Condemned2Recomp).

**Status: playable on Linux** — boot, menus, the robot builder and
career fights verified by playing, on Kubuntu (AMD RX 6600 / RADV),
with graphics, PipeWire audio, controller, and FSR upscaling.
**Windows build: in progress** via GitHub Actions (see
`.github/workflows/windows.yml`), following the same recipe that
produced 'Splosion Man's verified Windows test build.

> **No game data is included in this repository or its releases.**
> You must supply your own legally obtained copy of the game: the XBLA
> package for title ID **584111E0**, or a folder containing
> `default.xex` extracted from it. The launcher (below) can import and
> extract the XBLA package for you. Real Steel is © Yuke's / DreamWorks.
> This is a fan-made interoperability project; do not redistribute
> game assets.
>
> Note: the game's DLC robots were separate downloads on Xbox Live and
> are **not** part of the base package — roster slots belonging to DLC
> fighters stay locked without them. Online versus is not supported
> (the Xbox Live services it used are gone); local play is the target.

## Download

Linux builds ship as an AppImage plus a folder zip (see Releases):
extract, `chmod +x` the AppImage, run — the launcher auto-detects your
game folder (`~/Games`, mounted drives, next to the launcher) or lets
you pick it, and its **Import XBLA Package** button extracts a stock
XBLA package into a ready-to-play folder. Requirements: a distro with
glibc 2.43 (the current packages are built on a recent toolchain —
older-distro rebuilds are on the roadmap), Vulkan drivers for your
GPU, and PipeWire or PulseAudio.

## The launcher

The **Factual Metal Launcher** (yes, really) is the house TP launcher
shared with the other ports (`tp-launcher/` in this repo, per-game
themed from one codebase — this build wears cage-steel teal):

- **Game tab**: detection status, one-click XBLA package import
  (built-in STFS extractor — no external tools), PLAY.
- **Settings in four explained sections** (Graphics / Audio / Storage
  & Logs / Advanced), each with a plain-language note on what it does:
  resolution scale, FSR or bilinear presentation, FXAA, MSAA, VSync,
  frame limit, mute, your choice of save-data and log locations — plus
  a **Reset to Defaults** button. Every setting maps to a flag the
  runtime actually has.
- Settings persist between runs; the exact command line assembled for
  every launch is written to `last-command.txt` next to the logs.

Two title-specific defaults worth knowing about, both earned the hard
way during the port:

- **Game speed is the frame rate — keep the frame limit at 60.** Real
  Steel advances its game logic one step per frame, so 45 fps plays at
  three-quarter speed and an unlimited frame rate runs menus at 1.5x.
  The launcher defaults to a 60 fps limit (and migrates older settings
  that had it at 0). The runtime's limiter schedules exact 1/60 s
  deadlines, so it holds 60.00, not "about 58".
- **VSync is safe (and on by default).** It only makes presentation
  tear-free. Older builds also ran the guest's vblank at the real 60 Hz,
  which snapped any frame that missed 16.7 ms to 30 fps — half-speed
  fights; that strict mode is now opt-in (`--vsync_swap_wait=true`).
- **The launcher always passes
  `--gpu_allow_invalid_fetch_constants=true`.** Real Steel issues
  texture fetches with 'invalid' fetch constants thousands of times
  per session; without the runtime's tolerance those draws render as
  flickering green textures.

## Building from source

You need: the ReXGlue SDK (pinned commit, with the house patches and
FidelityFX enabled), clang 20, Qt6 dev, Vulkan headers, and
**PipeWire + SPA development headers** — without them SDL silently
builds *without* a PipeWire audio backend and the game has no sound on
PipeWire systems. (Ask us how we know.)

1. Clone the SDK at `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`,
   init its submodules, and apply `patches/house-sdk.patch`
   (`git apply` — the accumulated house fixes: input threading,
   codegen discovery, shader-cache and presentation work).
2. Codegen from your own `default.xex` (or use the included
   `generated/`): `rexglue codegen`, with the function hints in
   `realsteel_config.toml`. **If you re-run codegen you must redo the
   post-processing** before the tree will build and play: run
   `scripts/refix.sh` (it freezes the codegen rule in the build graph
   and rewrites the handful of calls codegen leaves as fatal stubs
   with `scripts/fix-tailcalls.py`). The `generated/` tree in this
   repo is already in that post-fix state — it's the tree that
   produced the shipping Linux build.
3. Build the game: CMake preset `linux-amd64-release` on Linux,
   `win-amd64-release` on Windows, with
   `-DREXSDK_DIR=<sdk tree> -DREXGLUE_ENABLE_FIDELITYFX=ON`.
   (FidelityFX defaults OFF and fails *silently* — the game just looks
   blurry. Check for it.)
4. Build the launcher in `tp-launcher/` (CMake, Qt6,
   `-DTP_GAME=realsteel`).

Note: **never mix** the executable and `librexruntime.so` /
`librexgpu-xenos.so` (or their Windows `.dll` counterparts) from
different build sets — it corrupts the heap.

## Performance work (house runtime, 2026-11)

The first release was slow in exactly the way a frame-locked game
shows it: in the 3D arena it rendered ~33 fps, so fights played at
roughly half speed. Profiling the runtime (headless, `eu-stack`
sampling, A/B per change on the same scene) found several runtime
bugs that are fixed in `patches/house-sdk.patch`:

| Fix | Arena fps (headless, uncapped) |
|---|---|
| First release | 32.9 |
| Per-draw vertex-fetch OOB trace off by default (it scanned every index buffer on the GPU thread to print a DEBUG line) | 36.5 |
| Multi-object waits are event driven (they busy-spun: the audio thread burned a core), register lookup table, timer thread blocks instead of spinning | 43.5 |
| Write-watch faults no longer parse `/proc/self/maps` under the global lock (it stalled the GPU thread ~1/3 of the time) | 56.0 |
| Consecutive GPU register writes are batched (as in Xenia-Canary) | **67.7** |

With the 60 fps limiter the arena and fights now hold a locked 60.00
in the same headless setup (which itself adds ~5 ms per frame), so
the game runs at its real speed. Pacing fixes: the limiter uses fixed
deadlines (it used to drift below 60), and VSync no longer drops a
late frame to 30 fps (see the quirks above).

Diagnostics: `--rs_perf_log=true` logs swaps/s, vblanks/s, frame-time
average/max and the guest clock rate every 2 s.

Testing: `tools/linux/headless_run.sh` runs the game on a private Xvfb
display with audio muted and real controllers disabled (see
`docs/headless.md` and `AGENTS.md`), so it can be tested on a desktop
that is in use.

## Known landmine: `/dev/shm` leaks (Linux)

The runtime backs guest memory with a ~4.8 GB `xenia_memory_*` file in
`/dev/shm` per run. Crashed or killed runs **leak** these files; when
`/dev/shm` fills, new runs die with a bus error (exit code 7)
immediately after "Guest memory arena mapped". The launchers vacuum
stale files before launching; if you're running the game directly,
check `df -h /dev/shm` and remove stale files belonging to dead
processes: `rm /dev/shm/xenia_memory_*`.

## Credits & disclosure

Full attribution for every library, tool and asset this project
uses — ReXGlue SDK, Xenia, Qt, 7-Zip, FidelityFX, FFmpeg, SDL and
more — lives in **[CREDITS.md](CREDITS.md)**, with license texts in
**[licenses/](licenses/)**. This project's own code is
**BSD 3-Clause** (see [LICENSE](LICENSE)), matching the ReXGlue
SDK's license; the game itself remains © Yuke's / DreamWorks /
Microsoft and no game data is distributed.

- **Yuke's** — for Real Steel, and for making heavy robots feel heavy.
- **ReXGlue SDK team and upstream rexglue contributors** — the
  recompiler and runtime that make this possible.
- The launcher is an original, clean-room design shared across the
  house ports (extraction, detection and settings model informed by
  the earlier launcher work on the Dante's Inferno and Condemned 2
  recompilations).
- This port was built by Jason Hixon with heavy AI assistance
  (Muse, by Meta) on the coding side, and tested the old-fashioned
  way: by playing it. First boot to playable took one evening; the
  robots were, in fact, factual metal.
