# DS / GBA / GBC feasibility on the R36S (RK3326): research notes

Date: 2026-11-13. Target: R36S, RK3326 (4x Cortex-A35 at about 1.3-1.5 GHz, Mali-G31 MP2, 1 GB), 640x480, mainline Linux 7.x, KMS/DRM, Mesa Panfrost, no X/Wayland, ALSA, evdev.

Labels used below:
- **[V]** verified: read directly in the cited source (repo file, README, issue, wiki).
- **[I]** inferred: my conclusion from the sources, not stated by them.
- **[U]** unverified or no source found.

Most evidence comes from the build recipes of the distributions that ship on RK3326 (ROCKNIX, dArkOS/ArkOS, AmberELEC, Batocera), read through the GitHub API. Forum and Reddit benchmark threads were mostly unreachable: Reddit is blocked to the fetch tool, and handhelds.wiki and the Emulation General Wiki returned 403. Hard per-game FPS numbers for A35 are therefore scarce. What the distributions choose to ship and set as default is the strongest signal available.

---

## 1. DS emulation speed on RK3326 / Cortex-A35

### What the RK3326 distributions ship and set as default

| Distro (RK3326 build) | Default NDS emulator | Other NDS options on RK3326 | Source |
|---|---|---|---|
| ROCKNIX | **DraStic standalone (`drastic-sa`)** | melonDS libretro, melonDS DS libretro, DeSmuME libretro (32-bit build), SkyEmu libretro, DSperate | [V] [projects/ROCKNIX/packages/virtual/emulators/package.mk](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/packages/virtual/emulators/package.mk), the `H700\|RK3326)` case under `### Nintendo DS` |
| ROCKNIX | n/a | **melonDS standalone (`melonds-sa`) is NOT built for RK3326/H700**; it is built for RK3399, RK3566, RK3576, RK3588 and newer | [V] same file, the `PKG_EMUS` lists per device |
| dArkOS (the successor of ArkOS for R36S and clones) | **DraStic** (listed first) | **DSperate**. No libretro DS core is listed at all for rk3326 | [V] [Emulationstation/es_systems.cfg.rk3326](https://github.com/christianhaitian/dArkOS/blob/main/Emulationstation/es_systems.cfg.rk3326) (`<name>nds</name>`: emulators `drastic`, `dsperate`) |
| ArkOS (legacy) | DraStic standalone, the only NDS emulator | none | [V] [ArkOS wiki: Emulators and Ports information](https://github.com/christianhaitian/arkos/wiki/ArkOS-Emulators-and-Ports-information) ("Nintendo DS / Emulator: drastic standalone") |
| AmberELEC (RG351P/M/V/MP) | DraStic 2.5.2.2 (aarch64) | n/a | [V] [AmberELEC packages/games/emulators/drastic/package.mk](https://github.com/AmberELEC/AmberELEC/blob/main/packages/games/emulators/drastic/package.mk) |

**[I]** Every RK3326 distribution defaults to DraStic. dArkOS offers no libretro DS core on RK3326, and ROCKNIX leaves melonDS standalone out of the RK3326 build. Taken together, that is strong evidence that melonDS and DeSmuME are not practical on A35 for most games.

### DraStic on RK3326

- **[V]** The distributions ship DraStic with automatic frameskip enabled by default on RK3326:
  - ROCKNIX RK3326 `drastic.cfg`: `frameskip_type = 2` (automatic), `frameskip_value = 3`, `threaded_3d = 1`. Source: [ROCKNIX drastic-sa config/RK3326/drastic.cfg](https://github.com/ROCKNIX/distribution/tree/main/projects/ROCKNIX/packages/emulators/standalone/drastic-sa/config/RK3326) (an R36S-specific `drastic.cfg.r36s` exists too).
  - dArkOS RK3326 configs (`drastic.cfg.351v`, `.rgb10`, `.rk2020`): `frameskip_type = 2`, `frameskip_value = 2`, `threaded_3d = 1`, `hires_3d = 0`. Source: [dArkOS drastic/configs](https://github.com/christianhaitian/dArkOS/tree/main/drastic/configs).
  - On the RK3566 (`353v`) dArkOS uses `frameskip_value = 1`. **[I]** The A35 needs more frameskip headroom than the A55.
- **[V]** The meaning of frameskip in DraStic, per its own readme: automatic mode skips only when emulation falls below full speed, and never more than N frames in a row. Source: DraStic readme, mirrored at [trngaje/advanced_drastic/drastic_readme.txt](https://github.com/trngaje/advanced_drastic/blob/master/drastic_readme.txt).
- **[V]** From a retailer blog, so low trust: Pokémon Diamond, Pearl, Platinum, HeartGold and SoulSilver "run at playable framerates" in DraStic with frameskip 1 on the R36S. Source: [Lumerk R36S Pokémon guide](https://www.lumerk.com/blogs/lumerktech/the-complete-r36s-pokemon-games-guide-everything-retro-trainers-need-to-know-about-handheld-emulation).
- **[V]** Frameskip in Black/White can break the building-entry transition, which relies on a frame capture. Source: [DraStic forum, "Glitch in Pokemon Black"](https://drastic-ds.com/viewtopic.php?t=2539).
- **[V]** DroiX's RG351M review reports DS frame drops in heavier games such as Mario Kart, while lighter games run well. Source: [DroiX RG351M review](https://droix.net/blogs/en-au/anbernic-rg351m-review/).
- **[V]** Headroom on a faster core, for scale. DraStic r2.5.2.2 aarch64 on an RK3566 (A55 at 1.992 GHz) renders Super Mario 64 DS at 2.98 ms per frame (about 5.6x real time) and Meteos (2D) at 0.75 ms per frame (22x). Source: [DSperate docs/techniques/00-method-and-measurements.md](https://github.com/beebono/DSperate/blob/main/docs/techniques/00-method-and-measurements.md).
- **[I]** An A35 at 1.3-1.5 GHz is roughly 2-2.5x slower per core than an A55 at 2 GHz. That still leaves DraStic, in principle, at or above real time for 2D-heavy titles like the Pokémon games. The default frameskip settings above suggest the margin is thin in busy scenes (towns, double battles, the 3D overworld of Gen 5).
- **[U]** I found no per-game, frame-accurate benchmark of HGSS, Platinum or BW on an A35.

### melonDS (standalone and libretro) on A35

- **[V]** melonDS has an ARM64 JIT, and the libretro melonDS core builds it on Linux arm64: `ifeq ($(ARCH),arm64) JIT_ARCH=aarch64`, with sources `ARMJIT_A64/*`. The JIT toggle is "JIT Enable (Restart)". Source: [libretro/melonDS Makefile](https://github.com/libretro/melonDS/blob/master/Makefile), [Makefile.common](https://github.com/libretro/melonDS/blob/master/Makefile.common), [libretro_core_options.h](https://github.com/libretro/melonDS/blob/master/src/libretro/libretro_core_options.h).
- **[V]** Even on a much faster SoC (RK3568, A55 up to 1.992 GHz), melonDS 0.9.3 does not reach full speed in a 2D game: 53-54 fps in Tetris DS gameplay against 59.9 needed, with the main thread at 98-100% of one core. melonDS DS 1.3.1 was **8% slower** than 0.9.3 on that chip. Source: [korri-os/korri issue #341](https://github.com/korri-os/korri/issues/341).
- **[I]** On the A35, which is slower per clock and lower-clocked than that A55, melonDS (JIT or not) will run well below full speed. It is not viable for the Pokémon DS games. melonDS standalone also needs Qt6 (ROCKNIX's `melonds-sa` depends on `qt6`), which is heavy for 1 GB of RAM and not KMS-native. Source: [V] [ROCKNIX melonds-sa/package.mk](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/packages/emulators/standalone/melonds-sa/package.mk).

### DeSmuME / desmume2015 libretro

- **[V]** The DeSmuME libretro core has **no AArch64 dynarec**. The Makefile says: "AArch64 (ARM64): no dynarec backend available ... DESMUME_JIT_ARM = 0". A JIT exists only for 32-bit ARM. Source: [libretro/desmume Makefile.libretro](https://github.com/libretro/desmume/blob/master/desmume/src/frontend/libretro/Makefile.libretro).
- **[V]** That is why ROCKNIX builds `desmume-lr` as a **32-bit** core for RK3326 (`EMUS_32BIT="box86 desmume-lr gpsp-lr pcsx_rearmed-lr yabadabadoo-lr"`). Source: the ROCKNIX `emulators/package.mk` above.
- **[I]** A 64-bit DeSmuME is interpreter-only and much too slow on A35. A 32-bit build needs an armhf userspace plus `CONFIG_COMPAT` (the A35 supports AArch32 at EL0). It would still be slower than DraStic: it is not the default anywhere on RK3326.

### Newer projects (as of Nov 2026)

| Project | What it is | License | Relevance to RK3326 |
|---|---|---|---|
| **DSperate** ([beebono/DSperate](https://github.com/beebono/DSperate)) | A new DS/DSi emulator (first commit 2026-08-20, v3.4.1 on 2026-11-08). Clean-room reimplementation of DraStic's techniques (ARM to AArch64/ARMv7 recompiler, NEON 2D, threaded software 3D) with melonDS-grade accuracy. KMSDRM page flips, fbdev, Wayland dmabuf, RGA, Vulkan or GLES 3.1 present. **[V]** README | **GPL-3.0** [V] | **[V]** Shipped for RK3326 by ROCKNIX (option, with per-device configs for r33s, oga, ogs, rgb20s and others: [dsperate-sa/config/RK3326](https://github.com/ROCKNIX/distribution/tree/main/projects/ROCKNIX/packages/emulators/standalone/dsperate-sa/config/RK3326)) and by dArkOS ([DSperate/configs/dsperate.ini.rk3326](https://github.com/christianhaitian/dArkOS/blob/main/DSperate/configs/dsperate.ini.rk3326)). It is measured on an RK3566 (A55); **[U]** no A35 performance data was found. It is the only open-source candidate with DraStic-class speed. |
| **melonDS DS** ([JesseTG/melonds-ds](https://github.com/JesseTG/melonds-ds)) | Remake of the libretro melonDS core, v1.4.0 (2026-09-27) | GPL-3.0 [V] | Too slow, see the korri issue above. |
| **SuperDrastic** ([JorreFog/SuperDrastic](https://github.com/JorreFog/SuperDrastic)) | LD_PRELOAD companion library for Linux DraStic r2.5.2.2: renders DS screens straight to KMS scanout buffers, even frame pacing, audio pump, shaders, RetroAchievements | MIT [V] (DraStic itself not included) | Today it targets dual-panel devices (RG DS). "Single-screen handhelds: planned for the next release" [V]. The technique is highly relevant, see section 3. |
| **advanced_drastic** ([trngaje/advanced_drastic](https://github.com/trngaje/advanced_drastic)) | LD_PRELOAD hook (`libadvdrastic.so`) for 64-bit DraStic: layouts, backgrounds, custom menu | **No license file; binary-only hook** [V] | README lists "rocknix: rk3566, rk3326, ogu devices" as verified [V]. |
| **steward-fu/nds** ("drastic-steward") ([steward-fu/nds](https://github.com/steward-fu/nds)) | "NDS Emulator Based on DraStic Reverse Engineering". Patched SDL2 plus binary detours for Miyoo Mini, TrimUI and others | LGPL-2.1 [V] | Mostly 32-bit Miyoo-class devices; see section 3. |
| **NooDS** ([Hydr8gon/NooDS](https://github.com/Hydr8gon/NooDS)) | DS/GBA emulator | GPL-3.0 [V] | No JIT found in the repo; the Linux frontend uses wxWidgets (needs X or Wayland) [V]. **[I]** Not suitable. |
| **SkyEmu** (libretro core `skyemu-lr`) | GB/GBA/DS emulator | [U] | ROCKNIX lists it as an NDS option on RK3326 [V]; **[U]** no performance data. |

---

## 2. DraStic: license, redistribution, runtime requirements

- **Owner [V]:** DraStic is "by Exophase (exophase@gmail.com)" (readme header). The Android version is a joint effort with Lordus. The readme's FAQ: "The Pandora version will remain free. Other ports vary." Source: [drastic_readme.txt](https://github.com/trngaje/advanced_drastic/blob/master/drastic_readme.txt).
- **License [V/U]:** The DraStic readme bundled with the Linux build contains **no license or EULA text**. A grep for licen, distribut and copyright found only the author line and the FAQ quoted above. RetroBIOS describes it as "closed source and distributed as a binary". Source: [RetroBIOS DraStic page](https://abdess.github.io/retrobios/emulators/drastic/). **[U]** I could not find any published redistribution grant from Exophase for the Linux aarch64 build.
- **The DSperate author's claim [V, author's statement]:** "The debug-symbol build of DraStic ... is freely available and approved for distribution by its original developer, Exophase (who has since gone quiet)." Source: [DSperate techniques/00](https://github.com/beebono/DSperate/blob/main/docs/techniques/00-method-and-measurements.md). This is a third party's claim, not a primary statement from Exophase.
- **Build provenance [V]:** r2.5.2.2, AArch64 PIE, unstripped, DWARF `comp_dir /home/odroid/nds_emu/src/linux_odroid64`, md5 `17550db727f3b59d36b57746ad1944be`. Source: [RetroBIOS](https://abdess.github.io/retrobios/emulators/drastic/), [advanced_drastic README checksums](https://github.com/trngaje/advanced_drastic). **[I]** It was built for the ODROID-GO Advance/Super (Hardkernel), which explains why every RK3326 distribution uses it.
- **Android [V, secondary source]:** DraStic was removed from Google Play in February 2025 (Tech Insider, low-trust source). The last Android version on APKMirror is r2.6.0.4a (2024-03-06). Sources: [APKMirror](https://www.apkmirror.com/apk/exophase/drastic-2/). **[I]** Upstream is dormant; nobody can fix bugs in the closed binary.
- **How distributions ship it [V]:** all of them **bundle the binary into the image at build time** from a community-hosted tarball. None downloads it at runtime from Exophase.
  - ROCKNIX: `PKG_URL="https://github.com/ROCKNIX/packages/raw/main/drastic.tar.gz"`, `PKG_LICENSE="proprietary"`. It copies `drastic_aarch64/*` into `/usr/config/drastic`. The package description still says "will download bin on first run", but the recipe bundles it. [ROCKNIX drastic-sa/package.mk](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/packages/emulators/standalone/drastic-sa/package.mk); tarball committed 2024-03-18 in [ROCKNIX/packages](https://github.com/ROCKNIX/packages).
  - Batocera: `DRASTIC_VERSION = r2.5.2.2`, `DRASTIC_SITE = https://github.com/dmanlfc/drastic/raw/refs/heads/main`. [batocera drastic.mk](https://github.com/batocera-linux/batocera.linux/blob/master/package/batocera/emulators/drastic/drastic.mk)
  - AmberELEC: `PKG_URL="https://github.com/Cebion/packages/raw/main/misc/drastic.tar.gz"`. [AmberELEC package.mk](https://github.com/AmberELEC/AmberELEC/blob/main/packages/games/emulators/drastic/package.mk)
  - dArkOS: the binary is committed directly in the repo (`drastic/bin/drastic`, 1,581,200 bytes). [dArkOS drastic/bin](https://github.com/christianhaitian/dArkOS/tree/main/drastic/bin)
- **[V]** Another project's open question (korri-os): "whether Korri may host the DraStic binary in its plugin cache, or must fetch it from its author, is unknown." Source: [korri issue #341](https://github.com/korri-os/korri/issues/341).
- **[I] Recommendation:** treat redistribution as **legally unclear but widely tolerated**. The cleanest option for an original product is to *not* bundle DraStic. Either let the user drop the binary in, with the frontend detecting it by md5, or default to the GPL DSperate. Ask Exophase or a lawyer if bundling matters commercially.

### Runtime requirements of the Linux aarch64 binary

- **[V] SDL2:** SuperDrastic states its requirement as "the Linux DraStic build (r2.5.2.2, aarch64, SDL2)". advanced_drastic's comparison table: v2.5.2.x audio output is "SDL2" (v2.5.0.4 32-bit used ALSA). Its install guide tells users to remove `libSDL2-2.0.so.0` from the libs folder when switching to the LD_PRELOAD hook, which shows the binary links `libSDL2-2.0.so.0` dynamically. Sources: [SuperDrastic README](https://github.com/JorreFog/SuperDrastic), [advanced_drastic README](https://github.com/trngaje/advanced_drastic), [install.md](https://github.com/trngaje/advanced_drastic/blob/master/install.md).
- **[V] Rendering path:** DraStic uses the **SDL_Renderer API**. Each DS screen is an SDL **streaming texture** (ARGB8888, 256x192, or 512x384 with `hires_3d`), filled with `SDL_LockTexture`/`UnlockTexture` and drawn with `SDL_RenderCopy`, then `SDL_RenderPresent`. The menu is one 800x480 RGB565 streaming texture. SDL picks the `opengles2` renderer (or `opengl` on desktop GL). Sources: comments in [SuperDrastic src/dsflip.c](https://github.com/JorreFog/SuperDrastic/blob/main/src/dsflip.c) and the `SDL_CreateTexture` hook in ROCKNIX's [libdrastouch.c](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/packages/emulators/standalone/drastic-sa/sources/libdrastouch.c).
- **[V] KMS/DRM without X works:**
  - DSperate's author notes DraStic's "KMSDRM path segfaults without a DRM master", so DraStic does run on SDL's KMSDRM backend when it holds DRM master. Source: [DSperate techniques/00](https://github.com/beebono/DSperate/blob/main/docs/techniques/00-method-and-measurements.md).
  - SuperDrastic runs it with `SDL_VIDEODRIVER=dummy` and does the KMS scanout itself. Source: [SuperDrastic docs/INTEGRATION.md](https://github.com/JorreFog/SuperDrastic/blob/main/docs/INTEGRATION.md).
  - **[I]** ArkOS/dArkOS run without X on RK3326, so SDL2 KMSDRM plus GLES2 is the proven path. ROCKNIX RK3326 instead runs it under sway (Wayland): `DISPLAYSERVER="wl"`, `WINDOWMANAGER="swaywm-env"` in [devices/RK3326/options](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/devices/RK3326/options).
- **[U]** The exact `DT_NEEDED` list and the minimum glibc symbol version. **[I]** Run `readelf -d drastic` and `objdump -T drastic | grep GLIBC_` on the binary to settle it. The binary dates from about 2020, so glibc is very likely not a constraint.
- **[V] Input:** DraStic reads SDL keyboard and joystick events, with key codes in `drastic.cfg` (`controls_a[...]`). Distributions run `gptokeyb` to map the pad. Stylus is mouse emulation; ROCKNIX sets `SDL_TOUCH_MOUSE_EVENTS=0` plus its hook to fix real touch. Source: [ROCKNIX start_drastic.sh](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/packages/emulators/standalone/drastic-sa/scripts/start_drastic.sh).

---

## 3. Screen layout control

### DraStic built-in (Linux build)

- **[V] Options (readme):**
  - Screen orientation: horizontal (left to right), vertical (top to bottom), or single.
  - Screen scaling: None; Stretch aspect; **1x:2x** and **2x:1x** (one screen 256x192 and the other 512x384, which is DraStic's "one large, one small"; not available in single mode); TV split (Pandora only).
  - Screen swap: swaps the positions, or in single mode which screen is shown.
  - Pandora hotkeys: Z switches horizontal/vertical, X swaps screens, C switches single/double.

  Source: [drastic_readme.txt](https://github.com/trngaje/advanced_drastic/blob/master/drastic_readme.txt). **[V]** There is no free-form "hybrid" layout in the Linux build. That exists only in the Android version and in the hook projects below.
- **[V] `drastic.cfg` keys:** `screen_orientation`, `screen_scaling`, `screen_swap`, `frameskip_type`, `frameskip_value`, `safe_frameskip`, `threaded_3d`, `hires_3d`, `fix_main_2d_screen` ("keep main screen on top", useful for single-screen), `interframe_blend`, `mirror_touch`. Sources: ROCKNIX RK3326 drastic.cfg; [Batocera emulator.py](https://github.com/batocera-linux/batocera.linux/blob/master/python-src/batocera-launch-drastic/batocera_launch_drastic/emulator.py) and [drastic.emulator.yml](https://github.com/batocera-linux/batocera.linux/blob/master/package/batocera/emulators/drastic/drastic.emulator.yml).
- **[V] `screen_orientation` values:**
  - Batocera maps Vertical = 0, Horizontal = 1, Single (Top) = 2, and "Single (Bottom)" = 2 plus `screen_swap = 1`.
  - dArkOS uses `screen_orientation = 3` on RK3326 configs.
  - **[I]** Decompiled DraStic64 `update_input` shows the two hotkeys XOR the orientation with 1 (vertical/horizontal) and with 2 (single/dual). Source: [steward-fu/nds re-drastic/64/decom/exports/update_input.c](https://github.com/steward-fu/nds/blob/main/re-drastic/64/decom/exports/update_input.c). The value is therefore a 2-bit field: bit0 = horizontal, bit1 = single. Value 3 means single screen with the horizontal flag set.
- **[I] `screen_scaling` values,** by readme order: 0 none, 1 stretch, 2 1x:2x, 3 2x:1x. dArkOS uses `screen_scaling = 2` on RGB10/RK2020. Verify on the device.
- **[V] Hotkeys** are `drastic.cfg` bindings:
  - `controls_a/b[CONTROL_INDEX_SWAP_SCREENS]`
  - `CONTROL_INDEX_SWAP_ORIENTATION_A` (vertical/horizontal)
  - `CONTROL_INDEX_SWAP_ORIENTATION_B` (single/dual)
  - plus `MENU`, `SAVE_STATE`, `LOAD_STATE`, `FAST_FORWARD`, `TOUCH_CURSOR_*`, `HINGE`, `FAKE_MICROPHONE`, `QUIT`

  Values: below 1024 are SDL keys, 1024 and up are joystick buttons and axes (inferred from the code ranges). ArkOS RG351V: L2 = swap upper/lower screen, R2 = swap dual/single view. Sources: R36S cfg in ROCKNIX; [ArkOS FAQ RG351V](https://github.com/christianhaitian/arkos/wiki/Frequently-Asked-Questions---RG351V).
- **[I] Runtime control from a frontend:** there is no IPC. You can (a) rewrite `drastic.cfg` before launch, as ROCKNIX and Batocera do with `sed`, (b) inject the hotkey through SDL events or uinput, or (c) take full control with a hook (below).

### Community hooks: how they work

| Project | Mechanism [V] | License | Generic aarch64 / RK3326? |
|---|---|---|---|
| **ROCKNIX `libdrastouch.so`** ([source](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/packages/emulators/standalone/drastic-sa/sources/libdrastouch.c)) | `LD_PRELOAD` interposition of SDL2 symbols: `SDL_CreateWindow`, `SDL_SetWindowSize`, `SDL_CreateRenderer`, `SDL_RenderSetLogicalSize`, `SDL_CreateTexture`, `SDL_RenderCopy`, `SDL_RenderPresent`, `SDL_PollEvent`, audio-device calls. It identifies the DS screen textures by size (512x384 or 256x192 streaming) and the 32x32 stylus textures. Per `SDL_RenderCopy` it records the touch rect and optionally replaces the copy with a GLES2 shader pass (sharp-bilinear, LCD3x, NDS colour and others). It also adds a real microphone through SDL capture and a noise gate. | ROCKNIX package files are GPL-2.0 (SPDX header in package.mk) [V]; the .c has no header [U] | **Yes**: built from source for every ROCKNIX aarch64 device, RK3326 included. |
| **SuperDrastic / libdsflip** ([repo](https://github.com/JorreFog/SuperDrastic)) | `LD_PRELOAD` with `SDL_VIDEODRIVER=dummy`. It hands DraStic **DRM dumb buffers on `SDL_LockTexture`**, so DraStic writes each DS screen straight into scanout memory. An atomic-commit presenter thread does the pacing; touch is read from evdev and injected via `SDL_PollEvent`; there is an audio pump. | MIT [V] | aarch64 Linux with KMS atomic. Today it needs two panels; single-screen support is "planned for the next release" [V]. |
| **advanced_drastic** ([repo](https://github.com/trngaje/advanced_drastic)) | `LD_PRELOAD=libadvdrastic.so`, "need to use LD_PRELOAD instead of libSDL2* to hook drastic functions". It hooks 64-bit DraStic and builds its own settings and layout screens for the detected resolution. A `layout.json` defines background images ([trngaje/drastic_layout](https://github.com/trngaje/drastic_layout)). Hotkeys: Select+Left/Right cycles layout index, R2 swaps screens, Select+Y theme, Select+B blur/pixel. | **No license; ships prebuilt .so** [V] | README claims rocknix rk3326 works [V]; dArkOS uses it on RK3566 [V]. |
| **steward-fu "drastic-steward"** ([steward-fu/nds](https://github.com/steward-fu/nds)) | Ships its **own patched SDL2** (`sdl2/` tree) that DraStic links against, plus **binary detours**: `detour/hook.c` `mprotect`s DraStic's code pages and patches instructions at fixed addresses (e.g. `CODE_FAST_FORWARD`). Adds display modes (including a book-style mode and rotation 0/90/270), background images and a custom menu. Changelog v1.6-v1.8. | LGPL-2.1 [V] | Originally 32-bit Miyoo/TrimUI, with `drastic64` present; the changelog's last tag is "final-china-devices (2026/05/14)" [V]. Address-based detours are tied to an exact binary build. |

**[V/I] Can a frontend get the two 256x192 screens as separate buffers?** Yes, via an SDL2 hook. This is proven by libdrastouch (texture identification) and SuperDrastic (buffer substitution).

DraStic allocates one streaming texture per DS screen and draws it with `SDL_RenderCopy`. A preload library that interposes `SDL_CreateTexture`/`SDL_LockTexture`/`SDL_UnlockTexture`/`SDL_RenderCopy`/`SDL_RenderPresent` can receive each screen's pixels (ARGB8888) and composition order, discard DraStic's own `dstrect` layout, and compose freely: own layouts, backgrounds, hybrid, PiP. With `SDL_VIDEODRIVER=dummy` it can also own KMS output directly. Touch must then be remapped by injecting SDL mouse events in DraStic's logical coordinates, as both hooks do.

**[I]** On RK3326 the pragmatic composition paths are:
- GLES2/3 on Panfrost: upload two textures and draw quads.
- KMS planes: each DS screen in its own dumb buffer on an overlay plane. Panfrost is not involved; whether the VOP on RK3326 has enough scalable planes needs checking.
- CPU NEON copies at 640x480.

---

## 4. libretro DS cores: layout options and swap

| Core | Layout option values | Swap / cycle at runtime | Speed on A35 |
|---|---|---|---|
| **melonDS (legacy libretro)** [V] [libretro_core_options.h](https://github.com/libretro/melonDS/blob/master/src/libretro/libretro_core_options.h) | `melonds_screen_layout`: Top/Bottom, Bottom/Top, Left/Right, Right/Left, Top Only, Bottom Only, Hybrid Top, Hybrid Bottom. Also `melonds_screen_gap`, `melonds_hybrid_small_screen`, `melonds_hybrid_ratio`, `melonds_swapscreen_mode` (Toggle/Hold) | **R2 = swap screens** (`swap_screens_btn = ... JOYPAD_R2` in [input.cpp](https://github.com/libretro/melonDS/blob/master/src/libretro/input.cpp)). Option changes apply at runtime: `retro_run` checks `GET_VARIABLE_UPDATE`, calls `check_variables`, then `SET_SYSTEM_AV_INFO` [V] ([libretro.cpp](https://github.com/libretro/melonDS/blob/master/src/libretro/libretro.cpp)). Only "OpenGL Renderer (Restart)" and "JIT Enable (Restart)" need a restart [V]. | Too slow [I] (korri: 53 fps on A55 at 2 GHz in Tetris DS). |
| **melonDS DS** [V] [constants.hpp](https://github.com/JesseTG/melonds-ds/blob/main/src/libretro/config/constants.hpp), [screen.hpp](https://github.com/JesseTG/melonds-ds/blob/main/src/libretro/config/definitions/screen.hpp) | Up to 8 layout slots (`melonds_screen_layout1..8`, `melonds_number_of_screen_layouts`) with values top-bottom, bottom-top, left-right, right-left, top, bottom, hybrid-top/bottom, flipped-hybrid-*, largescreen-top/bottom, flipped-largescreen-*, rotate-left, rotate-right, rotate-180. Hybrid ratio 2:1/3:1, hybrid small-screen mode, screen gap, secondary-screen scale. | **R3 = cycle to the next layout** (`_cycleLayoutButton = ... JOYPAD_R3` in [joypad.cpp](https://github.com/JesseTG/melonds-ds/blob/main/src/libretro/input/joypad.cpp)); swapping is done by putting top-bottom and bottom-top in the cycle. README: "cycle through up to 8 screen layouts ... at the push of a button". L3 = mic, Y (optional) = lid. | Slower than legacy melonDS (korri: -8%). Not viable [I]. |
| **DeSmuME (libretro)** [V] [libretro.cpp](https://github.com/libretro/desmume/blob/master/desmume/src/frontend/libretro/libretro.cpp) | `desmume_screens_layout`: top/bottom, bottom/top, left/right, right/left, top only, bottom only, hybrid/top, hybrid/bottom. Also `desmume_hybrid_layout_ratio`, `desmume_hybrid_layout_scale`, `desmume_hybrid_showboth_screens`, `desmume_screens_gap`, `desmume_frameskip`, `desmume_cpu_mode` (jit/interpreter) | **R3 = "Quick Screen Switch"**: flips top/bottom to bottom/top, left/right to right/left, top-only to bottom-only and so on at runtime [V]. L2 = lid, L3 = mic, R2 = tap stylus. | No aarch64 JIT. Only a 32-bit armhf build has a JIT [V]. Too slow for Pokémon [I]. |
| **desmume2015** | Older fork; the repo has no detected license [V] (DeSmuME itself is GPL-2.0) | [U] | Slower still [I]. |

**[I]** A libretro core returns one composed framebuffer. If the frontend sets "top-bottom" with gap 0 and software rendering, it can split the 256x384 buffer into the two 256x192 screens and compose them itself. That makes libretro cores the easiest way to get separate screens. But none of these cores is fast enough on A35 for Pokémon DS.

**[V]** All three cores are GPL (melonDS GPL-3.0, melonDS DS GPL-3.0, DeSmuME GPL-2.0).

---

## 5. GBA / GBC cores on RK3326

| Core | License [V] | On RK3326 [V where stated] |
|---|---|---|
| **mGBA** (libretro, also `mgba_rumble`) | MPL-2.0 ([libretro/mgba](https://github.com/libretro/mgba), [mgba-emu/mgba](https://github.com/mgba-emu/mgba)) | **Default GBA core** on ROCKNIX RK3326 (`add_emu_core gba retroarch mgba true`) and on ArkOS (bold default in the [ArkOS emulator list](https://github.com/christianhaitian/arkos/wiki/ArkOS-Emulators-and-Ports-information)); listed first in dArkOS rk3326 es_systems [V]. **[I]** Full speed for most of the library, including Pokémon GBA. muOS users on the H700 (A53) report mGBA struggles mainly with fast-forward headroom, and a dev build regression ([muOS forum](https://community.muos.dev/t/canada-goose-gba-fast-forward-rate/859)). |
| **gpSP** (libretro) | GPL-2.0 | The dynarec supports "x86/x64, ARMv6/7 and ARMv8 and MIPS" [V] ([README](https://github.com/libretro/gpsp)). Note: ROCKNIX builds `gpsp-lr` as a **32-bit** core even on aarch64 devices (the package copies the `.arm` build) [V] ([gpsp-lr/package.mk](https://github.com/ROCKNIX/distribution/blob/main/projects/ROCKNIX/packages/emulators/libretro/gpsp-lr/package.mk)). dArkOS offers gpsp both under `retroarch` (64-bit) and `retroarch32` [V]. ArkOS notes `gba_bios.bin` is "required for lr-gpsp" [V], though the README says it ships an open BIOS replacement [V]. **[I]** It is the fastest GBA option and useful for fast-forward. Accuracy is lower than mGBA. |
| **Gambatte** (libretro) | GPL-2.0 ([libretro/gambatte-libretro](https://github.com/libretro/gambatte-libretro)) | **Default GB/GBC core** on ROCKNIX (`add_emu_core gbc retroarch gambatte true`) and on ArkOS/dArkOS [V]. **[I]** Full speed with large margin. |
| **SameBoy** (libretro) | **MIT/Expat** (upstream: "All files ... except for the iOS and HexFiend directories, are licensed under the Expat License"; [libretro/SameBoy](https://github.com/libretro/SameBoy) is detected as MIT) | Available as non-default on ROCKNIX and dArkOS [V]. **[U/I]** It is heavier (cycle-accurate) and probably still full speed on A35. Not verified. |
| armGBA (new) | GPLv2 ([MagnominusGit/armGBA](https://github.com/MagnominusGit/armGBA)) | A GBA/GB/GBC recompiler for ARM handhelds, an option in ROCKNIX and dArkOS (2026) [V]; no performance data [U]. |

**[I] Licensing note:** mGBA (MPL-2.0) and SameBoy (MIT) are the friendliest to bundle with an original, possibly non-GPL frontend. Gambatte and gpSP are GPL-2.0: fine to ship as separate libretro `.so` files, but linking them statically into a proprietary frontend would impose GPL. A libretro `.so` boundary is the usual separation; it is not a legal opinion.

---

## 6. What "fulldex" / "full dex" means

Likely meanings, in order of likelihood for a 2026 handheld-frontend context:

1. **"Pokémon Fulldex" (the project): most likely [V].** [mamesj/pokemon-fulldex](https://github.com/mamesj/pokemon-fulldex), GPL-3.0, created 2026-10-25, about 1,060 stars.
   - "An all-in-one launcher for Pokémon games (DS, GBA and 3DS)". It runs DS games on a built-in melonDS core, plus "a layer that knows what the game is doing (which screen, which menu, which battle) and rearranges it into one proper widescreen picture".
   - Touch-screen buttons go "beside the action". Widescreen 3D, 60 fps with in-between frames, RetroAchievements, cheats.
   - DS: BW2, BW, HGSS, Platinum, DP. GBA (via mGBA): Emerald, RS, FRLG and Ultra Violet. ORAS in beta.
   - It is a fork of [ClickingMaster/ssrecomp](https://github.com/ClickingMaster/ssrecomp) ("A recomp of Pokemon Soul Silver for single screen devices", GPL-3.0). Its successor is [pokeheartgold-single-screen](https://github.com/ClickingMaster/pokeheartgold-single-screen), MIT with an attribution requirement.
   - Press coverage: [Retro Dodo article](https://retrododo.com/pokemon-full-dex-converts-dual-screen-pokegames-to-a-single-screen-you-actually-enjoy-using/).
   - **[V]** Hardware bar: "Minimum: a 4-core processor (Intel Core i5 6th generation or Ryzen 1000 on) ... 4 GB of memory". The Switch runs at 60 fps with sys-clk overclocks. Android has been tested on a Pixel 9 Pro XL only.
   - **[I]** It is far beyond the R36S (A35, 1 GB): melonDS alone is too slow there, and its widescreen 3D layer needs GL 4.3 or a fast CPU. Its *idea* is directly relevant to an original DS frontend: game-aware screen composition for a single 640x480 panel, built on decompilation symbol maps (pret/pokeplatinum, pokeheartgold, the pokebw2 and aletheia symbol files).
2. **"Full National Dex" ROM hacks [V, developers' statements]:** hacks that make all 493 (Gen 4) or 649 (Gen 5) Pokémon obtainable in one save without trading or transfers.
   - Following Platinum and PlatinumQOL: "full national dex can be completed without trading or transferring" ([PokéCommunity thread](https://pokecommunity.com/threads/platinumqol-now-compatible-with-following-platinum.465574/)).
   - Drayano's Renegade Platinum: the fan wiki says every Pokémon from Bulbasaur to Arceus is obtainable ([renegade-platinum-wiki](https://zhenga8533.github.io/renegade-platinum-wiki)).
   - HeartGold VX expands availability but is not a full dex ([thread](https://www.pokecommunity.com/threads/pokemon-heartgold-vx-vanilla-expanded.489900/)).
3. **[I] Less likely:** a Pokédex companion view on the second screen. FullDex itself bundles companion additions "Based on PokeDaisy by Lidor Itzhari" ([lidor30/pokedaisy](https://github.com/lidor30/pokedaisy)) per its README [V]. Separately, "Pokédex editor for DS games" by Ravends is a ROM editing tool ([Project Pokémon profile](https://projectpokemon.org/home/profile/127833-ravends/)).

---

## Bottom line for the project [I]

- **DS on RK3326 means DraStic or DSperate.** DraStic r2.5.2.2 aarch64 is the proven default everywhere, with auto frameskip. Its license is unclear: closed, no EULA found, redistributed by all distros from community mirrors. DSperate (GPL-3.0, 2026) is the only open-source DraStic-class option and already ships for RK3326 in ROCKNIX and dArkOS, but it has no published A35 numbers. **Benchmark both on the R36S** with HGSS (overworld and double battles), Platinum (Jubilife), and BW/B2W2 (Castelia).
- **A custom layout with DraStic is feasible** with an MIT/GPL-style SDL2 `LD_PRELOAD` hook: intercept the two streaming textures, compose with GLES on Panfrost or KMS planes, inject touch. This is an original implementation of a proven technique (libdrastouch, SuperDrastic). Don't copy advanced_drastic, which is unlicensed.
- **DSperate already has** vertical, horizontal, single, PiP and two "dominant" layouts, hotkey cycling and swap, KMSDRM page flips and fbdev. As GPL-3.0 it can be modified or driven directly; it also documents a `[padhotkeys]` config.
- **libretro DS cores** (melonDS, melonDS DS, DeSmuME) have good runtime layout and swap controls but are too slow on A35.
- **GBA/GBC:** mGBA (MPL-2.0) default, gpSP (GPL-2.0) for speed, Gambatte (GPL-2.0) default for GB/GBC, SameBoy (MIT) as the accurate option. All are expected to be full speed on A35, except SameBoy, which is unverified.
- **"Fulldex"** most likely refers to the 2026 project Pokémon Fulldex (a game-aware single-screen launcher on melonDS). It is too heavy for the R36S, but it is a good design reference.
