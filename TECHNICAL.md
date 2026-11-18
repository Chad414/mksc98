# MKSC98 technical notes

Details behind the [README](README.md): the picture shapes, the test
tooling, and how the port is put together.

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
