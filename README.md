# Curvessor 2

![Curvessor GUI](Images/screenshot.jpg?raw=true 'screenshot')

[Curvessor](https://www.unevens.net/curvessor.html) is an audio plug-in that implements a dynamic range processor in which **the response curve of each channel is an automatable spline**.

Curvessor can be used as a classic compressor/expander, enjoying the freedom and precision that comes with spline editing.

But more specifically, as it allows the authoring of response curves _that are not increasing_, Curvessor can be used for creative dynamic range processing in sound design.

## Features

- The response curves are smoothly automatable splines.
- Optional Mid/Side Stereo processing.
- Forward, Feedback and Sidechain topologies.
- The amount of feedback can be smoothly changed, going from pure forward topology to pure feedback topology and everything in between. _(NEW in version 2)_
- Optional RMS and high-pass filtering on the level detector. _(NEW in version 2)_
- All parameters, and all splines, can have different values on the Left channel and on the Right channel - or on the Mid channel and on the Side channel, when in Mid/Side Stereo Mode.
- Dry-Wet.
- Up to 32x Oversampling with either Minimum Phase or Linear Phase Antialiasing.
- VU meter showing the difference between the input level and the output level.
- Customizable smoothing time, used to avoid zips when automating the knots of the splines, the stereo link percentage, the wet amount, or the input and output gains.

## Download

Pre-built binaries:

- **macOS** (universal — Apple Silicon + Intel): see the [Releases](https://github.com/unevens/Curvessor/releases) page. Each macOS zip contains the AU + VST3 bundles, the `legal/` folder, and a `README.txt` with install instructions (including the `xattr -dr com.apple.quarantine` step needed for unsigned OSS plug-ins).
- **Windows / Linux**: https://www.unevens.net/curvessor.html

## Build from source

Clone recursively:

```
git clone --recursive https://github.com/unevens/Curvessor
```

Curvessor uses the [JUCE](https://github.com/juce-framework/JUCE) C++ framework, referenced as a sibling checkout at `../JUCE`. Clone JUCE 8 next to this repo before building.

The authoritative build is CMake (`CMakeLists.txt` at the repo root). The old Projucer `.jucer` file has been removed — see git history if you need it.

### macOS

Apple's bundled Clang (Xcode 17, Command Line Tools 21) and any upstream Clang older than 22 hit a NEON-intrinsic codegen bug in `Source/CurvessorDsp.cpp`. The fix shipped in upstream LLVM 22, so build with **Homebrew Clang ≥ 22**:

```
brew install llvm

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++

cmake --build build -j8
```

`CMakeLists.txt` enforces this — configuring with Apple Clang or any clang older than 22 fails immediately with a message pointing at the right compiler. It also auto-derives `llvm-ar` / `llvm-ranlib` from the chosen compiler's toolchain dir.

This produces Standalone, AU, and VST3 in `build/Curvessor_artefacts/Release/`. The plug-ins are also copied to your user plug-in folders for an immediate DAW reload:

- `~/Library/Audio/Plug-Ins/Components/Curvessor 2.1.component`
- `~/Library/Audio/Plug-Ins/VST3/Curvessor 2.1.vst3`

#### Build options

| Option | Default | Description |
|---|---|---|
| `UNIVERSAL` | `ON` | Build a universal arm64+x86_64 binary so a single zip serves both Apple Silicon and Intel users. Disable with `-DUNIVERSAL=OFF` for ~2x faster single-arch dev iteration. |
| `INSTALL_TO_USER_PLUGINS` | `ON` | Copy AU/VST3 to `~/Library/Audio/Plug-Ins/*` after build. Disable with `-DINSTALL_TO_USER_PLUGINS=OFF` for CI builds or when you don't want the build to touch your live plug-in folder. |

#### Release zips

`cmake --build build --target package-zip` produces both a staging folder and a ready-to-upload zip:

```
build/release-zip/
├── Curvessor2.1_mac/
│   ├── Curvessor 2.1.component
│   ├── Curvessor 2.1.vst3
│   ├── legal/
│   └── README.txt
└── Curvessor2.1_mac.zip
```

The platform suffix becomes `_win` / `_linux` when built on those hosts.

### Windows

The CMake build is cross-platform via JUCE's `juce_add_plugin`. Windows is the next platform to be re-validated since the migration off Projucer. Adapt the `cmake` invocations above for MSVC, build, and please report any breakage.

### Linux

The Linux build worked under the old Projucer setup but is not actively tested right now. The CMake setup should be cross-platform via `juce_add_plugin`, but expect to fix things if you build there. PRs welcome.

## Submodules, libraries, credits

- [oversimple](https://github.com/unevens/oversimple) wraps two resampling libraries:
    - [HIIR](http://ldesoras.free.fr/prod.html) by Laurent de Soras — a 2x Upsampler/Downsampler with two-path polyphase IIR anti-aliasing filtering.
    - [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) by Aleksey Vaneev — a high-quality pro audio sample rate converter / resampler C++ library.
- [audio-dsp](https://github.com/unevens/audio-dsp), my toolbox for audio DSP and SIMD instructions, built on Agner Fog's [vectorclass](https://github.com/vectorclass/version2).
- [gamma-env](https://github.com/avaneev/gammaenv) — DSP S-curve envelope signal generator by Aleksey Vaneev. Curvessor uses a SIMD-optimized version of gammaenv I wrote specifically for it; see `audio-dsp/adsp/GammaEnv.hpp` and `audio-dsp/adsp/GammaEnvMacro.hpp`.
- [juicy](https://github.com/unevens/juicy), my JUCE-side UI/glue components (spline editor, attached controls, oversampling attachments).

## License

Curvessor is released under the GNU GPLv3 license. Full license text and third-party notices are in the [legal/](legal/) folder.

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.

## Version 1

For version 1, see the branch `version-1`.
