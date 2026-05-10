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

// IMPORTANT: do NOT include JuceHeader.h or anything that pulls JUCE in.
// The whole point of this TU is to compile the heavy spline + NEON intrinsic
// codegen in isolation from the JUCE headers, which together push the Apple
// Clang frontend over the bus-error threshold (see memory entry on the
// EmitBuiltinExpr crash).

#include "CurvessorDsp.h"

namespace curvessor {

namespace {

inline Vec2d
applyStereoLink(Vec2d in,
                Vec2d& stereo_link,
                Vec2d& stereo_link_target,
                Vec2d alpha)
{
  auto const mean = 0.5 * (in + permute2<1, 0>(in));
  stereo_link = stereo_link_target + alpha * (stereo_link - stereo_link_target);
  return in + stereo_link * (mean - in);
}

inline Vec2d
toVumeter(Vec2d vumeter_state, Vec2d env, Vec2d alpha)
{
  return env + alpha * (vumeter_state - env);
}

inline Vec2d
applyHighPassFilter(Vec2d input, Vec2d& state, Vec2d g)
{
  auto const v = g * (input - state);
  auto const low = v + state;
  state = low + v;
  return input - low;
}

constexpr double ln10 = 2.30258509299404568402;
constexpr double db_to_lin = ln10 / 20.0;

} // namespace

void
Dsp::forwardProcess(VecBuffer<Vec2d>& io,
                    int const numActiveKnots,
                    int const highPassOrder)
{
  auto spline = autoSpline.spline.getVecSpline();
  auto automation = autoSpline.automator.getVecAutomator();
  auto envelope = envelopeFollower.getVecData();

  auto stereo_link_target = Vec2d(stereoLinkTarget);
  auto automation_alpha = Vec2d(automationAlpha);

  auto stereo_link = Vec2d().load(stereoLink);
  auto gain_vumeter = Vec2d().load(gainVuMeterBuffer);
  auto level_vumeter = Vec2d().load(levelVuMeterBuffer);

  auto high_pass_state = Vec2d().load(highPassState);
  auto high_pass_coef = Vec2d().load(highPassCoef);
  auto high_pass_state_2 = Vec2d().load(highPassState2);
  auto high_pass_state_3 = Vec2d().load(highPassState3);

  int const numSamples = io.getNumSamples();

  for (int i = 0; i < numSamples; ++i) {

    Vec2d in = io[i];

    Vec2d env_in = in;

    if (highPassOrder >= 1) {
      env_in = applyHighPassFilter(in, high_pass_state, high_pass_coef);
    }

    if (highPassOrder >= 2) {
      env_in = applyHighPassFilter(env_in, high_pass_state_2, high_pass_coef);
    }

    if (highPassOrder >= 3) {
      env_in = applyHighPassFilter(env_in, high_pass_state_3, high_pass_coef);
    }

    Vec2d env_out = envelope.processDB(env_in);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc = spline.process(env_out, automation, numActiveKnots);

    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[i] = in * gc;
  }

  autoSpline.spline.update(spline, numActiveKnots);
  envelope.update(envelopeFollower);
  stereo_link.store(stereoLink);
  gain_vumeter.store(gainVuMeterBuffer);
  level_vumeter.store(levelVuMeterBuffer);
  high_pass_state.store(highPassState);
  high_pass_state_2.store(highPassState2);
  high_pass_state_3.store(highPassState3);
}

void
Dsp::feedbackProcess(VecBuffer<Vec2d>& io,
                     int const numActiveKnots,
                     int const highPassOrder)
{
  auto spline = autoSpline.spline.getVecSpline();
  auto automation = autoSpline.automator.getVecAutomator();

  auto envelope = envelopeFollower.getVecData();

  auto stereo_link_target = Vec2d(stereoLinkTarget);
  auto automation_alpha = Vec2d(automationAlpha);

  auto stereo_link = Vec2d().load(stereoLink);
  auto gain_vumeter = Vec2d().load(gainVuMeterBuffer);
  auto level_vumeter = Vec2d().load(levelVuMeterBuffer);

  auto feedback_amount_target = Vec2d().load(feedbackAmountTarget);
  auto feedback_amount = Vec2d().load(feedbackAmount);
  auto env_in = Vec2d().load(feedbackBuffer);

  auto high_pass_state = Vec2d().load(highPassState);
  auto high_pass_coef = Vec2d().load(highPassCoef);
  auto high_pass_state_2 = Vec2d().load(highPassState2);
  auto high_pass_state_3 = Vec2d().load(highPassState3);

  int const numSamples = io.getNumSamples();

  for (int s = 0; s < numSamples; ++s) {

    Vec2d in = io[s];

    feedback_amount =
      feedback_amount +
      automation_alpha * (feedback_amount_target - feedback_amount);

    env_in = in + feedback_amount * (env_in - in);

    if (highPassOrder >= 1) {
      env_in = applyHighPassFilter(env_in, high_pass_state, high_pass_coef);
    }

    if (highPassOrder >= 2) {
      env_in = applyHighPassFilter(env_in, high_pass_state_2, high_pass_coef);
    }

    Vec2d env_out = envelope.processDB(env_in);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc = spline.process(env_out, automation, numActiveKnots);

    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[s] = env_in = in * gc;
  }

  autoSpline.spline.update(spline, numActiveKnots);
  envelope.update(envelopeFollower);

  stereo_link.store(stereoLink);
  gain_vumeter.store(gainVuMeterBuffer);
  level_vumeter.store(levelVuMeterBuffer);
  env_in.store(feedbackBuffer);
  feedback_amount.store(feedbackAmount);
  high_pass_state.store(highPassState);
  high_pass_state_2.store(highPassState2);
}

void
Dsp::sidechainProcess(VecBuffer<Vec2d>& io,
                      VecBuffer<Vec2d>& sidechain,
                      int const numActiveKnots,
                      int const highPassOrder)
{
  auto spline = autoSpline.spline.getVecSpline();
  auto automation = autoSpline.automator.getVecAutomator();

  auto envelope = envelopeFollower.getVecData();

  auto automation_alpha = Vec2d(automationAlpha);
  auto stereo_link_target = Vec2d(stereoLinkTarget);

  auto stereo_link = Vec2d().load(stereoLink);
  auto gain_vumeter = Vec2d().load(gainVuMeterBuffer);
  auto level_vumeter = Vec2d().load(levelVuMeterBuffer);

  auto high_pass_state = Vec2d().load(highPassState);
  auto high_pass_coef = Vec2d().load(highPassCoef);
  auto high_pass_state_2 = Vec2d().load(highPassState2);
  auto high_pass_state_3 = Vec2d().load(highPassState3);

  int const numSamples = io.getNumSamples();

  for (int s = 0; s < numSamples; ++s) {
    Vec2d in = io[s];

    Vec2d env_in = sidechain[s];

    if (highPassOrder >= 1) {
      env_in = applyHighPassFilter(env_in, high_pass_state, high_pass_coef);
    }

    if (highPassOrder >= 2) {
      env_in = applyHighPassFilter(env_in, high_pass_state_2, high_pass_coef);
    }

    if (highPassOrder >= 3) {
      env_in = applyHighPassFilter(env_in, high_pass_state_3, high_pass_coef);
    }

    Vec2d env_out = envelope.processDB(env_in);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc = spline.process(env_out, automation, numActiveKnots);

    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[s] = in * gc;
  }

  autoSpline.spline.update(spline, numActiveKnots);
  envelope.update(envelopeFollower);
  stereo_link.store(stereoLink);
  gain_vumeter.store(gainVuMeterBuffer);
  level_vumeter.store(levelVuMeterBuffer);
  high_pass_state.store(highPassState);
  high_pass_state_2.store(highPassState2);
  high_pass_state_3.store(highPassState3);
}

} // namespace curvessor
