# Curvessor 2

![Curvessor GUI](Images/screenshot.jpg?raw=true 'screenshot')

[Curvessor](https://www.unevens.net/curvessor.html) is an audio plug-in that implements a dynamic range processor in which **the response curve of each channel is an automatable spline**.

Curvessor can be used as a classic compressor/expander, enjoying the freedom and precision that comes with spline editing.

But more specifically, as it allows the authoring of response curves _that are not increasing_, Curvessor can be used for creative dynamic range processing in sound design.

## Features

- The response curves are smoothly automatable splines.
- Optional Mid/Side Stereo processing.
- Forward, Feedback and Sidechain topologies. 
- The amount of feedback can be smoothly changed, going from pure forward topology to pure feedback topology and everything in between. _(NEW in verison 2)_
- Optional RMS and high-pass filtering on the level detector _(NEW in verison 2)_
- All parameters, and all splines, can have different values on the Left channel and on the Right channel - or on the Mid channel and on the Side channel, when in Mid/Side Stero Mode.
- Dry-Wet.
- Up to 32x Oversampling with either Minimum Phase or Linear Phase Antialiasing.
- VU meter showing the difference between the input level and the output level.
- Customizable smoothing time, used to avoid zips when automating the knots of the splines, the stereo link percentage, the wet amount, or the input and output gains.

## Build

Clone with

`git clone --recursive https://github.com/unevens/Curvessor`

Curvessor uses the [JUCE](https://github.com/WeAreROLI/JUCE) cross-platform C++ framework.

You'll need [Projucer](https://shop.juce.com/get-juce) to open the file `Curvessor.jucer` and generate the platform specific builds.

### macOS (Apple Silicon)

The CMake build at the repository root targets macOS. Apple's bundled Clang
(both Xcode 17's and Command Line Tools 21's) has a NEON-intrinsic codegen
bug that triggers a frontend bus error when compiling
`Source/CurvessorDsp.cpp`. The bug is fixed in upstream LLVM, so build with
**Homebrew Clang 22 or later**:

```
brew install llvm

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++

cmake --build build -j8
```

`CMakeLists.txt` enforces this — configuring with Apple Clang or any clang
older than 22 fails immediately with a message pointing at the right
compiler. It also auto-derives `llvm-ar` / `llvm-ranlib` from the chosen
compiler's toolchain dir, so you do not need to set `CMAKE_AR` /
`CMAKE_RANLIB` yourself. (Both are required because LTO is on
(`juce_recommended_lto_flags`), and the LLVM bitcode object files the
compiler emits cannot be archived by Apple's `/usr/bin/ar`.)

This builds Standalone, AU, and VST3 to `build/Curvessor_artefacts/Release/`.
After each successful build, JUCE also copies the AU and VST3 to the standard
user plugin folders:

- `~/Library/Audio/Plug-Ins/Components/Curvessor 2.1.component`
- `~/Library/Audio/Plug-Ins/VST3/Curvessor 2.1.vst3`

The Standalone `.app` is not auto-installed (no macOS convention for that) and
must be run from the build directory or copied manually. To disable the
auto-install, change `COPY_PLUGIN_AFTER_BUILD TRUE` to `FALSE` in
`CMakeLists.txt`.

## Supported platforms

Curvessor is developed and tested on Windows and Linux. macOS (Apple Silicon)
is supported via the CMake build above; see the macOS section.

VST and VST3 binaries are available at https://www.unevens.net/curvessor.html.

## Submodules, libraries, credits

- The [oversimple](https://github.com/unevens/oversimple) submodule is a wrapper around two resampling libraries:
    - [HIIR](https://github.com/unevens/hiir) by Laurent de Soras, *"a 2x Upsampler/Downsampler with two-path polyphase IIR anti-aliasing filtering"*.
    - [r8brain-free-src](https://github.com/avaneev/r8brain-free-src), *"an high-quality pro audio sample rate converter / resampler C++ library"* by Aleksey Vaneev.
- [audio-dsp](https://github.com/unevens/audio-dsp), my toolbox for audio dsp and SIMD instructions, which uses Agner Fog's [vectorclass](https://github.com/vectorclass/version2) and [Boost.Align](https://www.boost.org/doc/libs/1_71_0/doc/html/align.html).
- [gamma-env](https://github.com/avaneev/gammaenv): *"DSP S-curve envelope signal generator"*, by Aleksey Vaneev. Curvessor uses a SIMD optimized version of gammaenv that I wrote specifically for it. See the files `audio-dsp/adsp/GammaEnv.hpp` and `audio-dsp/adsp/GammaEnvMacro.hpp`.

Curvessor is released under the GNU GPLv3 license.

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.


## Version 1

For version 1, see the branch `version-1`.
