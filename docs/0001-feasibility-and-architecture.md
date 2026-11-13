# 0001: Feasibility and architecture

Status: **accepted** on 2026-11-13, with the skeleton before the benchmark
(see Milestones). The frontend is called **Vesper**. Revised the same day:
Vesper is written in C, and DSperate's core is built into it (see
Architecture).

Vesper is an original game frontend for the R36S under Lume (`~/dev/r36s-lightos`), for
GBA, GBC and Nintendo DS. It competes with slot; it shares no code or assets
with it. The sourced research behind this note is in
[`research-ds-feasibility.md`](research-ds-feasibility.md). In this note,
**[V]** marks a claim checked in a source, **[I]** an inference, and
**[M]** something to measure on the console.

## What it is for

- One shelf for GBA, GBC and DS games, booting straight from Lume.
- The DS's two screens on one 640x480 panel, in several layouts. One button
  cycles through them, one swaps top and bottom, and both work mid-game.
- A "FullDex-style" mode for the Pokémon DS games, after
  [mamesj/pokemon-fulldex](https://github.com/mamesj/pokemon-fulldex)
  (GPL-3.0). FullDex knows what the game is showing (overworld, menu,
  battle) and arranges the screens around that. Here the same idea becomes an
  **automatic layout**: the frontend picks the layout from the game's state.
  The widescreen 3D is left out, because the panel is 4:3 (see below).

## Feasibility

### DS: it means DraStic or DSperate

- **Every RK3326 distribution defaults to DraStic** [V] (ROCKNIX, dArkOS,
  ArkOS, AmberELEC). It runs with automatic frameskip (2 or 3) and threaded
  3D, which points to a thin speed margin on the A35 [I].
- **melonDS, melonDS DS and DeSmuME are too slow.** melonDS reaches only
  53-54 fps in Tetris DS on an A55 at 2 GHz, and the A35 is slower still
  (korri-os #341) [V]. ROCKNIX does not build melonDS standalone for RK3326,
  and dArkOS lists no libretro DS core for it [V]. DeSmuME has no AArch64 JIT
  [V].
- **FullDex itself will not run here.** It is built on melonDS, and its stated
  minimum is a 6th-generation i5 with 4 GB [V]. Its 16:9 widescreen 3D is also
  wasted on a 4:3 panel. What carries over is the idea, not the program.
- **DraStic** (r2.5.2.2 aarch64, by Exophase) is closed source and has no
  licence or EULA [V]. The distributions bundle it from community mirrors
  [V], and nobody has published a redistribution grant [U]. **We don't
  bundle it.** It stays an option for whoever copies it to the card, through
  the hook described below, after DSperate.
- **DSperate** ([beebono/DSperate](https://github.com/beebono/DSperate),
  GPL-3.0, August 2026) is a clean-room emulator that reimplements DraStic's
  techniques. It has KMSDRM output and its own layouts, and ROCKNIX and dArkOS
  already ship it for RK3326 [V]. Nobody has published A35 numbers [U]: it
  is built for and measured on the RK3566 (Cortex-A55) [V].
  **It is the default DS emulator, if it holds the speed** [M]. What makes it
  fit Vesper [V, README and source tree]:
  - C++17 with CMake, with an `aarch64-cross` preset and `DSPERATE_TUNE` for
    the CPU (here `-mcpu=cortex-a35`).
  - The emulator is in `src/core/`, with its own CMakeLists, apart from its
    frontends in `src/frontend/` (SDL, headless, video outputs). SDL2 is
    needed only by the SDL frontend. There is no public API and no libretro
    core: Vesper adds a thin wrapper of its own.
  - Its own layouts (vertical, horizontal, single, pip, dominant), a stylus
    crosshair driven by a stick, ten save-state slots.
  - No DS BIOS needed: a free BIOS and a generated firmware are built in. Only
    DSi mode needs real DSi dumps.
- **A benchmark, the second milestone** [M]: DraStic and DSperate on the R36S
  under Lume. Play HeartGold or SoulSilver (overworld and double battle),
  Platinum (Jubilife) and Black 2 / White 2 (Castelia), and record fps,
  skipped frames and CPU per core. That decides the DS backend.

### GBA and GBC

- **GBA: gpSP** (libretro, GPL-2.0, ARMv8 dynarec). It already holds 60 fps
  under Lume in slot [V, measured locally]. mGBA (MPL-2.0) is the default in
  the distributions [V], but slot's notes say it cannot hold 60 fps on the
  RK3326, so it stays an option and gets measured again [M].
- **GB/GBC: Gambatte** (GPL-2.0), with a wide margin [I]. SameBoy (MIT) is the
  accurate alternative, not measured [M].

### Screens on 640x480

A DS screen is 256x192 (4:3), like the panel.

| Layout | Scale | Placement | Notes |
|---|---|---|---|
| **Single** | 2.5x | 640x480, full screen | The other screen hidden; swap picks which one shows |
| **Single + PiP** | 2.5x + 0.75x | 640x480, plus 192x144 in a corner | The small screen semi-transparent; corner and opacity can be set |
| **Stacked** | 1.25x + 1.25x | 320x480 centred, x = 160 | The background shows in the two 160 px side bands |
| **Side by side** | 1.25x + 1.25x | 640x240 centred, y = 120 | Bands top and bottom |
| **Large + small** | 1.875x + 0.625x | 480x360 at (0, 60), 160x120 at (480, 60) | The small screen is for glancing; its text is unreadable |
| **Sharp (integer)** | 2x / 1x + 1x | 512x384 at (64, 48); 256x384 at (192, 48) | No interpolation, background around |

Non-integer scales go through a sharp-bilinear shader on the GPU: integer
scaling with nearest, then one bilinear step. The user picks which layouts
are in the cycle, and in what order.

**Buttons** (the DS has neither L2/R2 nor sticks, so these are free in DS
games):

- **R2**: next layout. **L2**: swap top and bottom.
- **Right stick**: the stylus cursor; **R3** touches. The cursor shows when
  the stick moves and hides after 2 s. In *Single* on the top screen, moving
  the stick brings up the bottom screen as a PiP until the touch ends.
- **FN**: the frontend's menu (save states, layouts, back to the shelf), as on
  the R36S's other systems.

**Speed of the switch:** the frontend composes both screens itself every
frame, so a new layout applies on the next vblank, under 16.7 ms. A
crossfade of 100-150 ms is optional.

Touch never depends on the layout. The frontend maps the cursor from the
bottom screen's rectangle to DS coordinates (0-255, 0-191) and sends the
emulator those, so the emulator always sees one fixed layout.

### Automatic layout (FullDex-style, Pokémon DS)

- Each game gets a profile: overworld → *Single* top with the bottom screen as
  PiP; battle → *Large + small* or *Stacked*; bag, Pokétch or C-Gear →
  *Single* bottom. A manual press always wins until the next scene change.
- **The game's state** is read from the DS's RAM, at addresses taken from the
  decompilations (pret/pokeplatinum, pokeheartgold, the BW2 symbol files).
  The addresses are facts; we copy no code from FullDex. Where reading RAM is
  not possible, a fallback recognises the bottom screen from a few sampled
  pixels.
- This needs the emulator to share its RAM, which is easy with DSperate (open
  source) and possible with DraStic through the hook (the binary is
  unstripped) [I]. It comes after the manual layouts.

## Architecture

```
            Lume init
               │  (frontend path, env, poweroff/reboot shims)
               ▼
   ┌───────────────────────────────────┐  DRM master for its whole life
   │  Vesper (C11), one process        │  KMS/DRM + GBM + EGL + GLES2 (Panfrost)
   │  shelf, menu, compositor,         │  evdev (devices open for its whole life)
   │  layouts, stylus cursor           │  ALSA plug:default 48 kHz
   │                                   │
   │  libretro host ───────────────────┼── gpsp / gambatte / mgba .so (dlopen)
   │  DS: ds_core.h (C API)            │
   │   └ thin C++ wrapper ─ DSperate   │  src/core, built in, GPL-3.0
   └───────────────────────────────────┘
        optional: DraStic in its own process + our LD_PRELOAD hook
```

- **Language: C11** for Vesper. KMS/DRM, GBM, EGL, GLES, ALSA, evdev and the
  libretro API are all C APIs, called directly with no bindings. The only C++
  is a thin wrapper around DSperate's core, which is C++17. The code that
  reads files from the card (ROM headers, zip, settings) stays small and is
  tested on the Mac with sanitizers (ASan, UBSan).
- **Build:** CMake, cross-compiled for aarch64 against Lume's userland
  (glibc 2.42, Mesa 26.0.1), with `-mcpu=cortex-a35`. DSperate's `src/core`
  comes in as a git submodule of our fork, built by the same CMake.
- **Video:** direct KMS/DRM with atomic commit and page flips on vblank, GBM
  and EGL, and GLES 2/3 on Panfrost. No SDL. Each frame is the game's
  textures, the background and the menu.
- **GBA/GBC:** a libretro host inside Vesper, with the cores as separate
  `.so` files. Audio uses dynamic rate control against the vblank.
- **DS: DSperate's core built into Vesper.** The wrapper exposes a small C
  API, `ds_core.h`: load a ROM, run one frame, the two 256x192 screens, the
  pad and the touch point, the audio samples, a view of the main RAM, save
  and load state, write the battery save.
  - The two screens arrive as buffers in the same process: no shared memory,
    no second program drawing, no input handed over. One compositor, one
    cursor and one menu for every system, and the input devices stay open
    (no 40 ms close).
  - The main RAM is readable directly, which is what the automatic layout
    needs.
  - The cost: a crash in the emulator also closes Vesper. Vesper writes the
    battery save regularly, so a crash loses at most a few seconds.
  - The DS core runs on its own threads (CPU and 3D, as DSperate does),
    Vesper's main thread composes and flips; the four A35 cores are shared
    between them [M].
  - Changes to DSperate (the wrapper, any fix) go to our public fork, as
    GPL-3.0 requires.
- **DraStic, optional:** its own process, with our own `LD_PRELOAD` hook (an
  original implementation of a known technique, see libdrastouch and
  SuperDrastic). The hook intercepts `SDL_CreateTexture`, `SDL_LockTexture`,
  `SDL_UnlockTexture` and `SDL_RenderCopy`, copies the two screens to shared
  memory (about 393 KB per frame, 23 MB/s) and injects keys and touch through
  `SDL_PollEvent`. It needs `libSDL2` in Vesper's folder on the card, not in
  Lume. Comes after DSperate, and only if the benchmark calls for it.
- **Saving before power-off:** Lume's shims kill the caller with SIGKILL.
  Before calling `poweroff`, Vesper therefore writes the battery save (and,
  with DraStic, asks it to quit and waits for it, with a time limit), then
  runs `sync`.
- **Background:** the user's lilac and pink valley (confirmed)
  (`valley-landscape-...-6318x2633.jpg`), cropped to 4:3 around the centre
  and stored at 640x480. Colours: QuisLab's palette (lilac `#9D88EC`, night
  `#0E0C16`, warm white `#F2EEE8`). The image's licence for redistribution has
  to be checked before it ships in a release.
- **Licence: GPL-3.0-or-later** for Vesper, required now that DSperate
  (GPL-3.0) is built into it. The libretro cores stay separate `.so` files.

### Under Lume

Today Lume's init starts only `/flash/slot/System/slot`. The proposal, a
change for the Lume repo that is not made here: init reads the frontend's path
from a file on the card (for example `lightos/frontend`). It gives the
frontend the same environment and the same `poweroff`/`reboot` shims, with
neutral names (`FRONTEND_ROOT`, `FRONTEND_RUN`). Until then, the frontend
runs from Lume's USB disk mode or by hand. Lume has glibc 2.42, Mesa with
GBM/EGL/GLES and ALSA. It has no SDL2: Vesper with DSperate built in does
not need it [I, SDL2 is used only by DSperate's SDL frontend]. The benchmark
(DSperate's own SDL frontend) and DraStic do, so for them `libSDL2` goes in
Vesper's folder on the card.

## Milestones

The order was changed at the go-ahead: the skeleton comes first, then the
benchmark.

1. **Frontend skeleton:** KMS/GLES, evdev, ALSA, a shelf with the
   background, GBA/GBC through libretro.
2. **Benchmark** DSperate as it is (its SDL frontend on KMSDRM, built for
   the A35), DraStic for comparison, and gpSP and mGBA, on the console.
   If DSperate holds the speed, it is the DS emulator.
3. **DSperate in Vesper:** the wrapper and `ds_core.h`, the layouts, R2/L2,
   the stylus cursor.
4. **Menu and saves:** save states, settings, a clean power-off.
5. **Automatic layout** for HGSS, Platinum and BW/BW2.
