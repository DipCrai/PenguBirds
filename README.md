# PenguBirds

Run the classic PC Angry Birds games natively on Linux — no Wine, no
emulator, no VM.

PenguBirds takes the game out of your own retail installer, recompiles its
x86 code to C++ with [SwARc](https://github.com/JTM-rootstorm/SwARc), builds
a native binary on your machine and adds a desktop launcher. No game files
are included; you supply an installer for a copy you own.

## Supported games

| Game | Installer version |
|---|---|
| Angry Birds | 4.0.0 |
| Angry Birds Seasons | 4.1.0 |
| Angry Birds Rio | 2.2.0 |
| Angry Birds Space | 2.0.0 |
| Angry Birds Star Wars | 1.5.0 |
| Angry Birds Star Wars II | 1.5.1 |

Only these exact builds work. Anything else shows up as `unsupported build`
and is refused.

## Requirements

64-bit Linux with a working Vulkan driver, and these packages (Arch names):

```sh
sudo pacman -S --needed clang cmake ninja 7zip \
  sdl3 ffmpeg openssl glslang spirv-tools mpg123 vulkan-icd-loader
# GPU driver: vulkan-radeon / nvidia-utils / vulkan-intel
```

`7z` must be on `PATH`; it unpacks the installer's payload. No network
access, Wine, Ghidra or SwARc checkout is needed for the release.

## Quick start

Download the latest archive from the
[Releases page](https://github.com/DipCrai/PenguBirds/releases), then:

```sh
tar -xzf pengubirds-1.0-linux-x86_64.tar.gz
cd pengubirds-1.0-linux-x86_64
./pengubirds
```

Put the installers in the same directory as `pengubirds`, or run it from the
directory that has them. The TUI lists every supported game it finds:

| Key | Action |
|---|---|
| `↑` / `↓` | move |
| `space` | select several games |
| `enter` | install, or remove the highlighted installed game |
| `q` | quit |

## What gets installed

| Path | Contents |
|---|---|
| `~/.local/share/pengubirds/games/<id>/` | game binary, extracted game data, runtime libs |
| `~/.local/share/applications/pengubirds-<id>.desktop` | desktop entry |
| `~/.local/bin/pengubirds-<id>` | launcher |
| `~/.local/share/<id>/` | saves |

The first install compiles the game and takes several minutes. Uninstalling
removes the game and its saves; the TUI asks for confirmation first.

## Build from source

Install the release requirements, plus:

```sh
sudo pacman -S --needed git pkgconf llvm freetype2 fontconfig zlib vulkan-headers
```

```sh
git clone https://github.com/DipCrai/PenguBirds.git pengubirds
cd pengubirds
tools/pengubirds/package.sh
```

`package.sh` clones SwARc at a pinned commit into `out/deps`, applies
`patches/swarc-local.patch`, builds the tool, lifter and runtime, and writes
the release to `out/release/pengubirds-1.0-linux-x86_64/`.
`tools/pengubirds/clean.sh` removes `out/deps` and `out/build` and keeps the
release.

## How it works

1. **Unpack.** PenguBirds parses the Advanced Installer SFX itself and
   unpacks the embedded `FILES.7z` with system `7z`.
2. **Lift.** `recomp_lift` translates the game exe and its `msvcr100`,
   `msvcp100`, `grgl1`/`grgl2` DLLs from x86 to C++, guided by the symbol
   packs and manifests shipped in the release.
3. **Build.** CMake, Ninja and Clang compile that into one native binary
   linked against SwARc's Win32 and OpenGL layers, SDL3's GPU backend
   (Vulkan), FFmpeg and mpg123.

The result is a single native process: no interpreter, emulator or Wine at
run time. The symbol packs and manifests are analysis metadata, so a known
build can be recompiled without Ghidra.

## License

PenguBirds is MIT; see [LICENSE](LICENSE). SDL3, FFmpeg, OpenSSL, glslang,
SPIRV-Tools and mpg123 are not bundled — install them from your distro and
respect their licenses (zlib, LGPL, Apache-2.0 and others).

No game code, assets, DLLs or keys are distributed and PenguBirds is not
affiliated with Rovio Entertainment. The activation compatibility layer only
re-implements what the installer you own would have done; it does not unlock
paid content. The software is provided "as is".
