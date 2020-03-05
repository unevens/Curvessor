/*
Copyright 2020 Dario Mambro

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

#include "PluginProcessor.h"
#include "adsp/GammaEnvMacro.hpp"
#include "adsp/SplineMacro.hpp"

static void
leftRightToMidSide(double** io, int const n)
{
  for (int i = 0; i < n; ++i) {
    double m = 0.5 * (io[0][i] + io[1][i]);
    double s = 0.5 * (io[0][i] - io[1][i]);
    io[0][i] = m;
    io[1][i] = s;
  }
}

static void
midSideToLeftRight(double** io, int const n)
{
  for (int i = 0; i < n; ++i) {
    double l = io[0][i] + io[1][i];
    double r = io[0][i] - io[1][i];
    io[0][i] = l;
    io[1][i] = r;
  }
}

static void
applyGain(double** io,
          double* gain_target,
          double* gain_state,
          double const alpha,
          int const n)
{
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < n; ++i) {
      gain_state[c] = gain_target[c] + alpha * (gain_state[c] - gain_target[c]);
      io[c][i] *= gain_state[c];
    }
  }
}

static inline __m128d
applyStereoLink(__m128d in,
                __m128d& stereo_link,
                __m128d& stereo_link_target,
                __m128d alpha)
{
  __m128d mean =
    _mm_mul_pd(_mm_set1_pd(0.5),
               _mm_add_pd(in, _mm_shuffle_pd(in, in, _MM_SHUFFLE2(0, 1))));

  stereo_link =
    _mm_add_pd(stereo_link_target,
               _mm_mul_pd(alpha, _mm_sub_pd(stereo_link, stereo_link_target)));

  return _mm_add_pd(in, _mm_mul_pd(stereo_link, _mm_sub_pd(mean, in)));
}

__m128d
toVumeter(__m128d vumeter_state, __m128d env, __m128d alpha)
{
  return _mm_add_pd(env, _mm_mul_pd(alpha, _mm_sub_pd(vumeter_state, env)));
}

constexpr double ln10 = 2.30258509299404568402;
constexpr double db_to_lin = ln10 / 20.0;

void
CurvessorAudioProcessor::forwardProcess(
  VecBuffer<Vec2d>& io,
  adsp::SplineInterface<Vec2d>* spline,
  adsp::SplineAutomatorInterface<Vec2d>* automator,
  double const automationAlpha)
{
  int const numKnots = spline->getNumKnots();

  LOAD_SPLINE_STATE(spline, numKnots, Vec2d, maxNumKnots);
  LOAD_SPLINE_AUTOMATOR(automator, numKnots, Vec2d, maxNumKnots);
  LOAD_GAMMAENV_STATE(envelopeFollower, Vec2d);

  __m128d stereo_link = _mm_load_pd(stereoLink[0]);
  __m128d stereo_link_target = _mm_load_pd(stereoLinkTarget[0]);
  __m128d gain_vumeter = _mm_load_pd(gainVuMeterBuffer[0]);
  __m128d level_vumeter = _mm_load_pd(levelVuMeterBuffer[0]);
  __m128d automation_alpha = _mm_set1_pd(automationAlpha);

  int const numSamples = io.getNumSamples();

  for (int i = 0; i < numSamples; ++i) {

    Vec2d in = io[i];

    SPILINE_AUTOMATION(spline, automator, numKnots, Vec2d);

    Vec2d env_out;
    COMPUTE_GAMMAENV(envelopeFollower, Vec2d, in, env_out);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc;
    COMPUTE_SPLINE(spline, numKnots, Vec2d, env_out, gc);
    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[i] = in * gc;
  }

  STORE_SPLINE_STATE(spline, numKnots);
  STORE_GAMMAENV_STATE(envelopeFollower, Vec2d);
  stereoLink[0] = stereo_link;
  gainVuMeterBuffer[0] = gain_vumeter;
  levelVuMeterBuffer[0] = level_vumeter;
}

void
CurvessorAudioProcessor::feedbackProcess(
  VecBuffer<Vec2d>& io,
  adsp::SplineInterface<Vec2d>* spline,
  adsp::SplineAutomatorInterface<Vec2d>* automator,
  double const automationAlpha)
{
  int const numKnots = spline->getNumKnots();

  LOAD_SPLINE_STATE(spline, numKnots, Vec2d, maxNumKnots);
  LOAD_SPLINE_AUTOMATOR(automator, numKnots, Vec2d, maxNumKnots);
  LOAD_GAMMAENV_STATE(envelopeFollower, Vec2d);

  __m128d stereo_link = _mm_load_pd(stereoLink[0]);
  __m128d stereo_link_target = _mm_load_pd(stereoLinkTarget[0]);
  __m128d gain_vumeter = _mm_load_pd(gainVuMeterBuffer[0]);
  __m128d level_vumeter = _mm_load_pd(levelVuMeterBuffer[0]);
  __m128d automation_alpha = _mm_set1_pd(automationAlpha);

  Vec2d env_in = feedbackBuffer[0];

  int const numSamples = io.getNumSamples();

  for (int i = 0; i < numSamples; ++i) {

    Vec2d in = io[i];

    SPILINE_AUTOMATION(spline, automator, numKnots, Vec2d);

    Vec2d env_out;
    COMPUTE_GAMMAENV(envelopeFollower, Vec2d, env_in, env_out);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc;
    COMPUTE_SPLINE(spline, numKnots, Vec2d, env_out, gc);
    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[i] = env_in = in * gc;
  }

  feedbackBuffer[0] = env_in;

  STORE_SPLINE_STATE(spline, numKnots);
  STORE_GAMMAENV_STATE(envelopeFollower, Vec2d);
  stereoLink[0] = stereo_link;
  gainVuMeterBuffer[0] = gain_vumeter;
  levelVuMeterBuffer[0] = level_vumeter;
}

void
CurvessorAudioProcessor::sidechainProcess(
  VecBuffer<Vec2d>& io,
  VecBuffer<Vec2d>& sidechain,
  adsp::SplineInterface<Vec2d>* spline,
  adsp::SplineAutomatorInterface<Vec2d>* automator,
  double const automationAlpha)
{
  int const numKnots = spline->getNumKnots();

  LOAD_SPLINE_STATE(spline, numKnots, Vec2d, maxNumKnots);
  LOAD_SPLINE_AUTOMATOR(automator, numKnots, Vec2d, maxNumKnots);
  LOAD_GAMMAENV_STATE(envelopeFollower, Vec2d);

  __m128d stereo_link = _mm_load_pd(stereoLink[0]);
  __m128d stereo_link_target = _mm_load_pd(stereoLinkTarget[0]);
  __m128d gain_vumeter = _mm_load_pd(gainVuMeterBuffer[0]);
  __m128d level_vumeter = _mm_load_pd(levelVuMeterBuffer[0]);
  __m128d automation_alpha = _mm_set1_pd(automationAlpha);

  int const numSamples = io.getNumSamples();

  for (int i = 0; i < numSamples; ++i) {
    Vec2d in = io[i];

    SPILINE_AUTOMATION(spline, automator, numKnots, Vec2d);

    Vec2d env_in = sidechain[i];
    Vec2d env_out;
    COMPUTE_GAMMAENV(envelopeFollower, Vec2d, env_in, env_out);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc;
    COMPUTE_SPLINE(spline, numKnots, Vec2d, env_out, gc);
    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[i] = in * gc;
  }

  STORE_SPLINE_STATE(spline, numKnots);
  STORE_GAMMAENV_STATE(envelopeFollower, Vec2d);
  stereoLink[0] = stereo_link;
  gainVuMeterBuffer[0] = gain_vumeter;
  levelVuMeterBuffer[0] = level_vumeter;
}

void
CurvessorAudioProcessor::processBlock(AudioBuffer<double>& buffer,
                                      MidiBuffer& midi)
{
  ScopedNoDenormals noDenormals;

  auto const totalNumInputChannels = getTotalNumInputChannels();
  auto const totalNumOutputChannels = getTotalNumOutputChannels();
  auto const numSamples = buffer.getNumSamples();

  double* ioAudio[2] = { buffer.getWritePointer(0), buffer.getWritePointer(1) };

  // prepare to process

  oversamplingGetter.update();
  auto& oversampling = oversamplingGetter.get();

  bool const isMidSideEnabled = parameters.midSide->get();

  bool const isSideChainAvailable = totalNumOutputChannels == 4;

  Topology topology = static_cast<Topology>(parameters.topology->getIndex());

  bool const isSideChainRequested = topology == Topology::SideChain;

  if (isSideChainRequested && !isSideChainAvailable) {
    topology = Topology::EmptySideChain;
  }

  bool const isUsingSideChain = topology == Topology::SideChain;

  stereoLinkTarget[0] = 0.01 * parameters.stereoLink->get();

  auto [spline, automator] = parameters.spline->updateSpline(splines);

  double const frequencyCoef = 1000.0 * MathConstants<double>::twoPi /
                               (getSampleRate() * oversampling.getRate());

  double inputGainTarget[2];
  double outputGainTarget[2];

  for (int c = 0; c < 2; ++c) {

    outputGainTarget[c] = exp(db_to_lin * parameters.outputGain.get(c)->get());
    inputGainTarget[c] = exp(db_to_lin * parameters.inputGain.get(c)->get());

    envelopeFollowerSettings.setup(
      c,
      parameters.envelopeFollower.metric.get(c)->getIndex() == 1,
      frequencyCoef / parameters.envelopeFollower.attack.get(c)->get(),
      frequencyCoef / parameters.envelopeFollower.release.get(c)->get(),
      parameters.envelopeFollower.attackDelay.get(c)->get(),
      parameters.envelopeFollower.releaseDelay.get(c)->get());
  }

  float const smoothingTime = parameters.smoothingTime->get();

  double const automationAlpha =
    smoothingTime == 0.f ? 0.f : exp(-frequencyCoef / smoothingTime);

  // mid side

  if (isMidSideEnabled) {
    leftRightToMidSide(ioAudio, numSamples);
  }

  // input gain

  applyGain(ioAudio, inputGainTarget, inputGain, automationAlpha, numSamples);

  // early return if no knots are active

  if (!spline) {
    // output gain

    applyGain(
      ioAudio, outputGainTarget, outputGain, automationAlpha, numSamples);

    // mid side

    if (isMidSideEnabled) {
      midSideToLeftRight(ioAudio, numSamples);
    }

    return;
  }

  automator->setSmoothingAlpha(automationAlpha);

  // oversampling

  oversampling.prepareBuffers(numSamples); // extra safety measure

  int const numUpsampledSamples =
    oversampling.scalarToVecUpsamplers[0]->processBlock(ioAudio, 2, numSamples);

  if (numUpsampledSamples == 0) {
    for (auto i = 0; i < totalNumOutputChannels; ++i) {
      buffer.clear(i, 0, numSamples);
    }
    return;
  }

  auto& upsampledBuffer = oversampling.scalarToVecUpsamplers[0]->getOutput();
  auto& upsampled_io = upsampledBuffer.getBuffer2(0);

  // sidechain

  if (isUsingSideChain) {
    double* envelopeInput[2] = { buffer.getWritePointer(2),
                                 buffer.getWritePointer(3) };

    if (isMidSideEnabled) {
      leftRightToMidSide(envelopeInput, numSamples);
    }

    applyGain(ioAudio,
              inputGainTarget,
              sidechainInputGain,
              automationAlpha,
              numSamples);

    oversampling.scalarToVecUpsamplers[1]->processBlock(
      envelopeInput, 2, numSamples);
  }

  auto& upsampled_env_input =
    oversampling.scalarToVecUpsamplers[1]->getOutput().getBuffer2(0);

  // processing

  switch (topology) {

    case Topology::Feedback:

      feedbackProcess(upsampled_io, spline, automator, automationAlpha);

      break;

    case Topology::Forward:

      forwardProcess(upsampled_io, spline, automator, automationAlpha);

      break;

    case Topology::SideChain:

      sidechainProcess(
        upsampled_io, upsampled_env_input, spline, automator, automationAlpha);

      break;

    case Topology::EmptySideChain:
      break;

    default:
      assert(false);
      break;
  }

  // downsample

  oversampling.vecToScalarDownsamplers[0]->processBlock(
    upsampledBuffer, ioAudio, 2, numSamples);

  // output gain

  applyGain(ioAudio, outputGainTarget, outputGain, automationAlpha, numSamples);

  // mid side

  if (isMidSideEnabled) {
    midSideToLeftRight(ioAudio, numSamples);
  }

  // update vu meters

  for (int i = 0; i < 2; ++i) {
    levelVuMeterResults[i].store(levelVuMeterBuffer[0][i]);
    gainVuMeterResults[i].store(gainVuMeterBuffer[0][i]);
  }
}
