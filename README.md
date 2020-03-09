# Curvessor

![Curvessor GUI](Images/screenshot.jpg?raw=true 'screenshot')

[Curvessor](https://www.unevens.net/curvessor.html) is an audio plug-in that implements a dynamic range processor in which **the response curve of each channel is an automatable spline**.

Curvessor can be used as a classic compressor/expander, enjoying the freedom and precision that comes with spline editing.

But more specifically, as it allows the authoring of response curves _that are not increasing_, Curvessor is meant be used for sound design.

## Features

- The response curves are smoothly automatable splines.
- Optional Mid/Side Stereo processing.
- Forward, Feedback and Sidechain topologies.
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

## Supported platforms

Curvessor is developed and tested on Windows and Linux. It may also work on macOS, but I can neither confirm nor deny.

VST binaries are available at https://www.unevens.net/curvessor.html.

You can also build Curvessor as a VST3 plugin, but there it has an issue with Ableton Live when reporting the latency during the plugin initialization, which can lead the a crash. The branch `vst3-latency-workaround` offers a work in progress fix for that, but for now I suggest to use the VST version.

## Submodules, libraries, credits

- The [oversimple](https://github.com/unevens/hiir) submodule is a wrapper around two oversampling libraries:
    - [My fork of the HIIR](https://github.com/unevens/hiir) library by Laurent de Soras, *"a 2x Upsampler/Downsampler with two-path polyphase IIR anti-aliasing filtering"*. My fork adds support for double precision floating-point numbers, and AVX instructions.
    - [My fork](https://github.com/unevens/r8brain/tree/include)  of [r8brain-free-src](https://github.com/avaneev/r8brain-free-src), *"an high-quality pro
  audio sample rate converter / resampler C++ library"* by Aleksey Vaneev.
- [audio-dsp](https://github.com/unevens/audio-dsp), my toolbox for audio dsp and SIMD instructions, which uses Agner Fog's [vectorclass](https://github.com/vectorclass/version2) and [Boost.Align](https://www.boost.org/doc/libs/1_71_0/doc/html/align.html).
- [gamma-env](https://github.com/avaneev/gammaenv): *"DSP S-curve envelope signal generator"*, by Aleksey Vaneev. Curvessor uses a SIMD optimized version of gammaenv that I wrote specifically for it. See the files `audio-dsp/adsp/GammaEnv.hpp` and `audio-dsp/adsp/GammaEnvMacro.hpp`.

Curvessor is released under the GNU GPLv3 license.

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
