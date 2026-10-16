# OpenRM95

OpenRM95 is an open source reimplementation of the game runtime (`GAME.EXE`) of
RPG Maker 95 (RPGツクール95). Its goal is to run games made with RPG Maker 95 on current
systems without modifying any game file.

The behaviour follows the last version of the original runtime, 1.23: map and event
processing, message and menu windows, battles, shops, inns, the 8-bit palette display with
its screen transitions and palette effects, MIDI/WAV playback and save files. Save files use
the format of the original runtime.

OpenRM95 is written in C++17 and uses SDL2, SDL2_mixer and FreeType, and optionally FFmpeg.

## Status

OpenRM95 is under development. Known differences from the original runtime:

* Game controllers are mapped to the keyboard instead of the joystick handling of the original runtime.
* AVI movies are shown in true colour and without their audio track.

Instead of the Windows file dialog of the original runtime, saving and loading use a small
in-game file list (`*.sav` files in the game folder).

## Building

Requirements:

* A C++17 compiler (GCC, Clang or MSVC)
* CMake 3.16 or newer
* SDL2, SDL2_mixer and FreeType (development packages)
* Optional: FFmpeg (libavcodec, libavformat, libswscale) for AVI movies in codecs other than the
  built-in ones (Cinepak, RLE8, uncompressed), e.g. Indeo. Disable with `-DOPENRM95_WITH_FFMPEG=OFF`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Debian/Ubuntu the dependencies are `libsdl2-dev libsdl2-mixer-dev libfreetype-dev` and
optionally `libavcodec-dev libavformat-dev libswscale-dev`.

## Running

```sh
openrm95 [options] <game folder>
```

The game folder is the folder that contains the `.RPG` file. Options:

| option | |
|---|---|
| `--window` / `--fullscreen` | start in a window (default) or in fullscreen |
| `--scale N` | window scale factor (default 2) |
| `--font PATH` | font file (a `.ttf`/`.ttc`; `PATH#N` selects face N of a collection) |
| `--soundfont PATH` | SoundFont used for MIDI playback |
| `--no-audio` | disable audio |
| `--load FILE` | start by loading a save file |
| `--verbose` | more log output |

Keys: arrow keys / numeric keypad to move, Enter or Space to confirm, Escape to cancel and to
open the menu, Shift to skip battle messages, Alt+Enter or F4 to toggle fullscreen, F12 to
save a screenshot. The mouse works as in the original runtime.

### Font

The original runtime draws all text with MS PGothic (ＭＳ Ｐゴシック, bold, 16 px). OpenRM95
looks for `font.ttf`/`font.ttc` in the game folder, then for `msgothic.ttc` and common
Japanese fonts of the system (IPAPGothic, Noto Sans CJK). For the most accurate layout use
MS PGothic with `--font`.

### MIDI

MIDI files are played by SDL_mixer. A SoundFont can be given with `--soundfont` (or the
`SDL_SOUNDFONTS` environment variable); otherwise the default MIDI backend of SDL_mixer is
used.

## References

* RPG Maker 95 file format documentation by Ghabry:
  <https://github.com/Ghabry/rpg95-fileformat>
* RPG Maker 95 runtime `GAME.EXE` version 1.23, analysed with Ghidra:
  <https://ghidra-sre.org/>
* Wine, used to run the original runtime for comparison: <https://www.winehq.org/>
* FFmpeg, used as reference for the Cinepak decoder and as optional movie decoder:
  <https://ffmpeg.org/>
* Win32 API documentation (window, GDI and multimedia functions used by the original runtime):
  <https://learn.microsoft.com/en-us/windows/win32/api/>

## License

OpenRM95 is free software under the MIT license, see [LICENSE](LICENSE).

RPG Maker (RPGツクール) is a trademark of its respective owners. OpenRM95 is an independent
project and is not affiliated with them.
