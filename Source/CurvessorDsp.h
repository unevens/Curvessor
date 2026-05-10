/*
Copyright 2020-2026 Dario Mambro

This file is part of Curvessor.

Curvessor is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Curvessor is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Curvessor.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

// JUCE-free DSP unit. Kept separate from PluginProcessor so that the heavy
// SIMD/template instantiation in CurvessorDsp.cpp compiles in a translation
// unit that does NOT include JuceHeader.h — this sidesteps the Apple Clang
// frontend bus error that fires on Source/Processing.cpp when JUCE and the
// spline NEON intrinsics are codegen'd in the same TU.

#include "adsp/GammaEnv.hpp"
#include "adsp/Spline.hpp"

namespace curvessor {

inline constexpr int maxNumKnots = 9;
inline constexpr int maxEditableKnots = maxNumKnots - 1;

using Spline = adsp::Spline<Vec2d, maxNumKnots>;
using AutoSpline = adsp::AutoSpline<Vec2d, maxNumKnots>;
using SplineAutomator = Spline::SmoothingAutomator;

struct Dsp
{
  AutoSpline autoSpline;

  adsp::GammaEnv<Vec2d> envelopeFollower;

  double stereoLink[2];
  double wetAmount[2];
  double inputGain[2];
  double outputGain[2];
  double sidechainInputGain[2];
  double feedbackBuffer[2];
  double feedbackAmount[2];
  double feedbackAmountTarget[2];
  double rmsAlpha[2];
  double levelVuMeterBuffer[2];
  double gainVuMeterBuffer[2];
  double highPassCoef[2];
  double highPassState[2];
  double highPassState2[2];
  double highPassState3[2];
  double automationAlpha;
  double stereoLinkTarget;

  Dsp()
  {
    AVEC_ASSERT_ALIGNMENT(this, Vec2d);
    std::fill_n(stereoLink, 2 * 15, 0.0);
  }

  void forwardProcess(VecBuffer<Vec2d>& io,
                      int const numActiveKnots,
                      int const highPassOrder);

  void feedbackProcess(VecBuffer<Vec2d>& io,
                       int const numActiveKnots,
                       int const highPassOrder);

  void sidechainProcess(VecBuffer<Vec2d>& io,
                        VecBuffer<Vec2d>& sidechain,
                        int const numActiveKnots,
                        int const highPassOrder);
};

} // namespace curvessor
