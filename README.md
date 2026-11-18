<p align="center">
  <img src="win98/mksc98-logo.svg" alt="MKSC98" width="500">
</p>

# MKSC98

An unofficial, cut-down port of the classic game from
[MKSC Neo](https://github.com/arcanite24/mksc-neo) to **Windows 98 SE** on
period hardware. It keeps MKSC Neo's original software mode-7 renderer, menus,
sprites, game logic and sound, played on the keyboard, and drops everything a
DirectX 7-era PC can't carry. The picture is the original 240x160 at the GBA's
59.73 Hz, scaled by whole numbers so pixels stay sharp. In fullscreen it can
grow to fill a 5:4, 4:3 or 16:9 screen without stretching.

MKSC98 is not affiliated with or endorsed by the MKSC Neo authors or Nintendo.
Please don't report MKSC98 problems to MKSC Neo.

Developed in a VMware Windows 98 SE guest and tested on a real Athlon 900 /
Radeon 7200 / Sound Blaster PCI machine at 1280x1024.

## Requirements

**Computer**
- Windows 98 SE (it also runs on later Windows)
- A CPU without SSE is fine: the target is a ~900 MHz Athlon, which holds full
  speed at 1280x1024
- DirectX 7 and a DirectX 7 graphics card for sharp hardware scaling.
  DirectDraw and plain GDI are used as fallbacks.
- Any DirectSound or waveOut sound card
- About 40 MB of disk space for the converted game data

No DLLs or installers are needed. `mksc98.exe` is one statically linked file
that imports only `kernel32`, `user32`, `gdi32`, `winmm` and `msvcrt`;
`ddraw.dll` and `dsound.dll` are loaded at run time when present.

**ROM.** No game content is included. You need your own legally obtained
cartridge dump of the US release:

| | |
|---|---|
| File name | `Mario Kart - Super Circuit (USA).gba` (must match exactly) |
| SHA-1 | `9d327c030c3e2d9007990518594f70c3340ac56f` |
| Format | Unzipped `.gba` |

Other regions, revisions and zipped ROMs are not supported. Never commit or
publish the ROM, `game.pack`, saves or screenshots taken from the game.

## Running it

1. Copy `mksc98.exe` to a folder on the Windows 98 machine, for example
   `C:\MKSC`.
2. Put the ROM in the same folder.
3. Run `mksc98.exe`. The first start converts the ROM into `game.pack`
   (about 38 MB) next to the exe. Later starts only check it.

Settings, records, ghosts, Grand Prix progress and `mksc98.log` are saved in
the same folder. The log is rewritten on every start; if something goes
wrong, it says why.

Recommended on a 1280x1024 screen:

```
mksc98.exe --fullscreen --aspect fill
```

Or turn on **Fullscreen** and **Aspect Ratio: Fill** once in Options; the game
then starts that way.

### Controls (keyboard only)

| Action | Key |
|---|---|
| Steer / move in menus | Arrow keys |
| Accelerate / confirm | Z (Enter also confirms) |
| Back in menus (does nothing during a race) | X |
| Brake | Down |
| Hop / drift | Left Shift |
| Item (aim with Up / Down) | C |
| Pause / start (leave a race from the pause menu) | Space or P |
| Quit the game | Esc, or **Quit** at the bottom of the title menu |

Driving keys can be rebound in Options. Esc always quits.

### Command-line options

```
--scale N          window size as a multiple of 240x160 (1..8); otherwise the saved Options value
--fullscreen       fill the screen at the desktop's own resolution and colour depth; the
                   picture is the largest whole multiple that fits, centred on black
--windowed         start in a window even if Fullscreen is saved in Options
--aspect R         for this launch: original, fill (see Picture shapes), or a fixed
                   shape W:H from 3:2 to 32:5
--tall N           races get N extra rows of sky on top (1..96) whatever the aspect setting
--fullscreen-640   switch the display to 640x480 16-bit instead (2x picture)
--no-flip          fullscreen Direct3D copies the picture to the screen instead of page-flipping
--sync-flip        fullscreen: wait inside the flip for the card to finish drawing
--ddraw            do not use Direct3D: DirectDraw stretch blit (some cards smooth it)
--gdi              use neither (GDI StretchDIBits)
--soft-scale       scale on the CPU and blit 1:1 instead of a hardware stretch blit
--no-audio         do not open the sound device
--waveout          sound through waveOut instead of a DirectSound looping buffer
--fps              log pictures per second every 5 seconds
```

### In-game Options

Title menu > Options lists only what works in this port:

| Option | Effect |
|---|---|
| Window Scale | Resizes the window (applies when leaving fullscreen if set while fullscreen). |
| Fullscreen | Desktop-resolution fullscreen, same as `--fullscreen`. |
| Display Scaling | Integer: whole-number scale with black borders (default, pixel perfect). The other setting stretches to fit. |
| Aspect Ratio | **Original** (240x160) or **Fill**. Fill applies in fullscreen; in a window the picture is the original. |
| Master Volume | 0 to 100. |
| Key Bindings | Z or Enter opens the eight keys; Tab resets the selected key. |
| Fast Menus, Input Display, Mirror Mode, Fix Doppler Bug | As in MKSC Neo. |

The title menu's Multiplayer row answers "unavailable".

## Picture shapes

The original picture is 240x160. Two things can be added without stretching a
single pixel; the result is always shown at a whole multiple of its own size,
centred on black.

- **Wider (`--aspect`)**: more columns (240 to 1024), showing more of the
  track to the sides. `--aspect 16:9` is 284 columns.
- **Taller (`--tall N`)**: N extra rows of sky above the race picture. The
  courses' sky artwork is taller than what the game normally shows, so these
  rows are real art. The road, the karts, the rank and the course map keep
  their place relative to the bottom edge. The lap/coin/item/time row and the
  portrait column move up to the new top edge.

**Fill** picks both for the screen in fullscreen: the largest whole scale,
every column that fits and every extra row that fits.

| Screen | Fill gives |
|---|---|
| 1280x1024 (5:4) | 256x204 at 5x = 1280x1020 |
| 1024x768 (4:3) | 256x192 at 4x = 1024x768 |
| 1920x1080 (16:9) | 320x180 at 6x = 1920x1080 |

The cup introduction fills the taller picture too. The result screens over a
race (lap times, results, Grand Prix points, finish portraits, the try-again
window) are centred in it, and the podium ceremony fills it with its own sky.
Menus and the title stay 160 lines tall, centred.

Known limits:
- Sprites and weather are drawn only within the original 160 lines, so
  anything rising above the old top edge is cut off at that line.
- Where a course's sky art runs out, its top row is repeated above it.
- Not every course has been looked at in the taller picture.
- A bigger picture costs render time roughly in proportion to its area.

## What's left out

3D renderer and 3D courses, SDL_GPU / OpenGL / GLES, HD-2D effects, modern
menus, Lua and mods, the mod store, Mario Kart DS / 64 imports, the course
editor, split-screen and local multiplayer, internal supersampling,
interpolation and unlocked frame rates, high-detail artwork, controllers,
rumble, touch, file dialogs, zipped ROMs, Android and web builds. MKSC Neo has
all of these.

## Building

MKSC98 is cross-compiled from Linux or WSL (Windows Subsystem for Linux) with
32-bit MinGW. On Ubuntu or Debian:

```sh
sudo apt install gcc-mingw-w64-i686 cmake ninja-build python3
git clone https://github.com/Chad414/mksc98.git
cd mksc98/win98
./build.sh            # out/mksc98.exe, then the post-build checks
./build.sh host       # headless reference build for the build machine
```

The exe is compiled with `-march=i686 -mtune=athlon-tbird -mno-sse -mno-sse2
-mfpmath=387`, static libgcc and the CRT startup code, linking against
`msvcrt.dll` (Ubuntu's `i686-w64-mingw32-gcc` 13 was used).

`check_exe.sh` runs after every build and fails it if:
- the exe imports anything not exported by a real Windows 98 SE DLL (the
  lists in `win98/exports/` were taken from a Win98 SE install with
  `fetch_exports.ps1`), or
- its disassembly contains any SSE/SSE2/AVX instruction.

## Testing

The exe has a scripted test mode:

```
--frames N         stop after N frames (with --bench: N race frames)
--hash             scripted Grand Prix; log a hash of every 60th and of the last frame
--dump-frame F.bmp write the last frame as a 24-bit BMP
--screenshot F.bmp save what the screen shows around the game window after the last frame
--bench            scripted Grand Prix with no vsync or sleep; ms per frame by phase
--watch            scripted Grand Prix in the window at normal speed with sound
--mode M           scripted run of gp (default), quick, battle or trial
--route C:D:S      cup (arena for battle), driver 1..8, class 0..2
```

Scripted runs use MKSC Neo's own input aid and a virtual clock, so the frames
are identical on every machine. To compare the Linux reference build with the
exe in a Windows 98 guest:

```
win98/run_host.sh "/path/to/rom.gba" --hash --frames 7200      # reference
win98/deploy.ps1 -Arguments "--hash --frames 7200" -Name hash  # guest
```

The `hash:` lines of the two logs must be identical.

`win98/vm.ps1` drives a VMware guest through `vmrun`. Set `MKSC_VMX` to the
guest's `.vmx`, and `MKSC_GUEST_USER` / `MKSC_GUEST_PASSWORD` if they are not
Administrator with no password. `deploy.ps1` copies the exe in, runs it and
brings the log back. Windows 98 turns the monitor off after 15 minutes
without input, and vmrun produces none, so set the guest's power scheme to
stay on or screenshots go stale.

Other helpers in `win98/`:
- `check_sharp.py`: is a `--screenshot` a pixel-exact enlargement of the `--dump-frame`?
- `bmp2png.py`: converts dumped frames.
- `gen_stubs.py`: regenerates `stubs.c` (see How it works).
- `dev_*.sh`: profiling, build variants and build-error summaries.

`--bench` plays a scripted race and reports average and worst ms per race
frame, split into logic, render, blit, audio and other, plus menu frames. The
budget is 16.74 ms.

### Results

Measured during development, against the full MKSC Neo tree where noted:
- MKSC Neo's C tests, built 32-bit with these flags: 245 of 247 passed. The
  two failures were float-equality checks in 3D renderer tests, which are out
  of scope.
- ROM import: the pack is byte-identical between 64-bit Linux and the 32-bit
  exe.
- Frame hashes: a scripted Grand Prix (7,200 frames) gives identical hashes on
  the Linux reference, on Windows 10 and in the Win98 SE guest, at 240 and 256
  columns.
- Time Trial: identical across those builds.
- Real PC (Athlon 900, Radeon 7200, 1280x1024, `--fullscreen --aspect fill`,
  256x204 at 5x): races average 16.5 ms per frame against the 16.74 ms budget.
  Of that, render is 8.3 ms and Direct3D presenting 3.8 ms. Menus average
  about 14 to 18 ms.

## How it works

| Path | Contents |
|---|---|
| `win98/` | The port: platform layer, presenters, build, tests, logo and licence. |
| `portable/` | The MKSC Neo source files the port compiles, trimmed to those that end up in the exe. |
| `tools/portable/modern_drift_format.py` | MKSC Neo's generator for two built-in data tables, run at build time. |

- **Platform layer.** MKSC Neo's game flow calls SDL3 directly. Those files
  are compiled unchanged against a small SDL3-shaped header
  (`win98/sdl3/SDL3/SDL.h`) implemented in `shim.c`. Underneath is
  `backend_win32.c`: plain ANSI Win32, nothing NT-only. There is no SDL at
  all, so the result is one self-contained exe. `backend_null.c` is a
  headless backend for reference builds on Linux.
- **Picture.** The frame is uploaded as a texture and drawn by Direct3D 7 as
  one point-sampled quad (`backend_d3d7.c`), so every game pixel becomes an
  exact square block and the card does the scaling. In fullscreen it is
  page-flipped without waiting for vertical sync. DirectDraw's stretch blit
  remains as the fallback, followed by GDI. DirectDraw has no filter control,
  and the Radeon 7200 smooths that blit.
- **Sound.** The game mixes 10512 Hz stereo, resampled to 22050 Hz. It plays
  through one looping DirectSound buffer (`backend_dsound.c`) that is started
  once and never stopped. Pauses and flushes overwrite queued sound with
  silence. Feeding the card through waveOut (still available as `--waveout`)
  page-faulted the Sound Blaster PCI driver (`sbpci`, exception 0E) after a
  few minutes of play.
- **Left out.** `win98/CMakeLists.txt` lists the MKSC Neo modules that are not
  compiled. `stubs.c` (generated by `gen_stubs.py`) gives every entry point the
  shared game shell still references an inert "feature absent" definition.
  Files the exe never links were removed from this repository; building
  without them gives a byte-identical exe.
- **Picture shapes.** MKSC Neo's renderer is not changed for these; `tall.c`
  wraps the scene, HUD and result-screen renderers.
- **Speed.** MKSC Neo's scheduler runs up to four game ticks per displayed
  picture when one takes too long, so game speed holds. The patch below
  removes 64-bit divisions from per-pixel loops (the internal render scale is
  the constant 1) and speeds up menu drawing and pack lookups, with identical
  output.

### Changes to MKSC Neo's files

Recorded in [`win98/upstream-changes.patch`](win98/upstream-changes.patch), all
under `MKSC_WIN98` or `PORTABLE_SOFTWARE_FRAME_NATIVE_ONLY`:

| File | Change |
|---|---|
| `platform/file_replace.h` | Windows 9x has no working `MoveFileEx`; saves are replaced with `MoveFile`/`DeleteFile`. |
| `platform/software_frame.h` | `PORTABLE_SOFTWARE_FRAME_NATIVE_ONLY` makes the internal scale a compile-time 1. |
| `platform/frontend_image_renderer.c` | Rotated/scaled images scan only the box that can contain them; inline pixel fetch with a per-image palette table; 32-bit stepping for the rotating background. |
| `assets/asset_pack.c` | Section lookup compares keys in place instead of decoding every entry. |
| `platform/gp_race_host.c` | X no longer drops a live race back to the menu; leave through the pause menu. |
| `app/frontend_flow.[ch]`, `platform/title_renderer.c`, `platform/game_host.c` | A Quit row below Options; Multiplayer reports unavailable; Options and key-binding lists cut down to what works; Enhancements, Mods and Graphics pages unreachable. |
| `platform/graphics_options.c` | Aspect Ratio toggles between Original and Fill. |

## Credits and licence

The game is [MKSC Neo](https://github.com/arcanite24/mksc-neo) by arcanite24:
the simulation, physics, AI, items, renderer, menus, sound and ROM importer
all come from there, and this port compiles it largely unchanged. MKSC Neo in
turn credits the [aplerdal/mksc](https://github.com/aplerdal/mksc)
decompilation for recovering the original game's behaviour. Third-party fonts
are listed in [LICENSE-NOTICE.md](LICENSE-NOTICE.md).

The port's own work in `win98/` and its changes to MKSC Neo's files are under
the MIT licence in [win98/LICENSE](win98/LICENSE). Everything else stays under
MKSC Neo's own terms.

MKSC98 is an unofficial fan project, not affiliated with, endorsed by or
supported by the MKSC Neo authors or Nintendo. Mario Kart and Mario Kart:
Super Circuit are trademarks of Nintendo. No Nintendo code, art, sound or ROM
data is included; you need your own legally obtained copy of the game.
