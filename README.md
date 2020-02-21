# Curvessor

![Curvessor GUI](screenshot.jpg?raw=true 'Curvessor')

[Curvessor](https://www.unevens.net/curvessor.html) is an audio plug-in that implements a dynamic range processor in which **the response curves of each channel are automatable splines**.

Curvessor can be used as a classic compressor/expander, enjoying the freedom and precision that comes with spline editing.

But more specifically, as it allows the authoring of response curves _that are not increasing_, Curvessor is meant be used for sound design.

## Features

- The response curves are smoothly automatable splines.
- Mid/Side Stereo.
- Sidechain.
- All parameters, and all splines, can have different values on the Left channel and on the Right channel - or on the Mid channel and on the Side channel, when in Mid/Side Stero Mode.
- Up to 32x Oversampling with either Minimum or Linear Phase.

## Build

Clone with

`git clone --recursive https://github.com/unevens/Curvessor`

Curvessor uses the [JUCE](https://github.com/WeAreROLI/JUCE) cross-platform C++ framework.

You'll need [Projucer](https://shop.juce.com/get-juce) to open the file `Curvessor.jucer` and generate the platform specific builds.

## Supported platforms

Curvessor is developed and tested on Windows and Linux. It may also work on macOS, but I can neither confirm nor deny.

VST and VST3 binaries are available at https://www.unevens.net/curvessor.html.

## Submodules, libraries, credits

- The [oversimple](https://github.com/unevens/hiir) submodule is a wrapper around two oversampling libraries:
    - [My fork of the HIIR](https://github.com/unevens/hiir) library by Laurent de Soras, *"a 2x Upsampler/Downsampler with two-path polyphase IIR anti-aliasing filtering"*. My fork adds support for double precision floating-point numbers, and AVX instructions.
    - [My fork](https://github.com/unevens/r8brain/tree/include)  of [r8brain-free-src](https://github.com/avaneev/r8brain-free-src), *"an high-quality pro
  audio sample rate converter / resampler C++ library"* by Aleksey Vaneev.
- [avec](https://github.com/unevens/avec), my toolbox/stash/library for SIMD and audio, which uses Agner Fog's [vectorclass](https://github.com/vectorclass/version2) and [Boost.Align](https://www.boost.org/doc/libs/1_71_0/doc/html/align.html).
- [gamma-env](https://github.com/avaneev/gammaenv): *"DSP S-curve envelope signal generator"*, by Aleksey Vaneev. Curvessor uses a SIMD optimized version of gammaenv that I wrote specifically for it. See the files `oversimple/avec/dsp/GammaEnv.hpp` and `oversimple/avec/dsp/GammaEnvMacro.hpp`.

Curvessor is released under the GNU GPLv3 license.

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
