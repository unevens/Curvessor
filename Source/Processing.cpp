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
    COMPUTE_GAMMAENV(envelopeFollower, Vec2d, in, env_out, true);

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
    COMPUTE_GAMMAENV(envelopeFollower, Vec2d, env_in, env_out, true);

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
    COMPUTE_GAMMAENV(envelopeFollower, Vec2d, env_in, env_out, true);

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

  double const frequencyCoef =
    1000.0 * MathConstants<double>::twoPi / (getSampleRate());

  double const upsampledFrequencyCoef = frequencyCoef / oversampling->getRate();

  float const smoothingTime = parameters.smoothingTime->get();

  double const automationAlpha =
    smoothingTime == 0.f ? 0.f : exp(-upsampledFrequencyCoef / smoothingTime);

  double const upsampledAutomationAlpha =
    smoothingTime == 0.f ? 0.f : exp(-upsampledFrequencyCoef / smoothingTime);

  double inputGainTarget[2];
  double outputGainTarget[2];
  double wetAmountTarget[2];

  for (int c = 0; c < 2; ++c) {

    outputGainTarget[c] = exp(db_to_lin * parameters.outputGain.get(c)->get());
    inputGainTarget[c] = exp(db_to_lin * parameters.inputGain.get(c)->get());

    wetAmountTarget[c] = 0.01 * parameters.wet.get(c)->get();

    // evenlope follower settings

    bool const rms = parameters.envelopeFollower.metric.get(c)->getIndex() == 1;

    double const attackFrequency =
      parameters.envelopeFollower.attack.get(c)->get();

    double const releaseFrequency =
      upsampledFrequencyCoef /
      parameters.envelopeFollower.release.get(c)->get();

    double const attackDelay =
      0.01 * parameters.envelopeFollower.attackDelay.get(c)->get();

    double const releaseDelay =
      0.01 * parameters.envelopeFollower.releaseDelay.get(c)->get();

    envelopeFollowerSettings.setup(
      c, rms, attackFrequency, releaseFrequency, attackDelay, releaseDelay);
  }

  if (automator) {
    automator->setSmoothingAlpha(upsampledAutomationAlpha);
  }

  bool const isWetPassNeeded = [&] {
    double m =
      wetAmountTarget[0] * wetAmountTarget[1] * wetAmount[0] * wetAmount[1];
    if (m == 1.0) {
      return false;
    }
    if (m == 0.0) {
      return !(wetAmountTarget[0] == 0.0 && wetAmountTarget[1] == 0.0 &&
               wetAmount[0] == 0.0 && wetAmount[1] == 0.0);
    }
    return true;
  }();

  bool const isBypassing = !isWetPassNeeded && (wetAmount[0] == 0.0);

  // mid side

  if (isMidSideEnabled) {
    leftRightToMidSide(ioAudio, numSamples);
  }

  // copy the dry signal

  dryBuffer.setNumSamples(numSamples);

  for (int c = 0; c < 2; ++c) {
    std::copy(ioAudio[c], ioAudio[c] + numSamples, dryBuffer.get()[c]);
  }

  // input gain

  applyGain(ioAudio, inputGainTarget, inputGain, automationAlpha, numSamples);

  // oversampling

  oversampling->prepareBuffers(numSamples); // extra safety measure

  int const numUpsampledSamples =
    oversampling->scalarToVecUpsamplers[0]->processBlock(
      ioAudio, 2, numSamples);

  oversampling->scalarToVecUpsamplers[1]->processBlock(
    dryBuffer.get(), 2, numSamples);

  if (numUpsampledSamples == 0) {
    for (auto i = 0; i < totalNumOutputChannels; ++i) {
      buffer.clear(i, 0, numSamples);
    }
    return;
  }

  auto& upsampledBuffer = oversampling->scalarToVecUpsamplers[0]->getOutput();
  auto& upsampledIo = upsampledBuffer.getBuffer2(0);
  auto& upsampledDryBuffer =
    oversampling->scalarToVecUpsamplers[1]->getOutput();

  // sidechain

  double* envelopeInput[2] = { buffer.getWritePointer(isUsingSideChain ? 2 : 0),
                               buffer.getWritePointer(isUsingSideChain ? 3
                                                                       : 1) };

  if (isUsingSideChain) {
    if (isMidSideEnabled) {
      leftRightToMidSide(envelopeInput, numSamples);
    }

    applyGain(ioAudio,
              inputGainTarget,
              sidechainInputGain,
              automationAlpha,
              numSamples);
  }

  oversampling->scalarToVecUpsamplers[2]->processBlock(
    envelopeInput, 2, numSamples);

  auto& upsampledSideChainInput =
    oversampling->scalarToVecUpsamplers[2]->getOutput().getBuffer2(0);

  // processing

  if (spline && !isBypassing) {

    switch (topology) {

      case Topology::Feedback:

        feedbackProcess(
          upsampledIo, spline, automator, upsampledAutomationAlpha);

        break;

      case Topology::Forward:

        forwardProcess(
          upsampledIo, spline, automator, upsampledAutomationAlpha);

        break;

      case Topology::SideChain:

        sidechainProcess(upsampledIo,
                         upsampledSideChainInput,
                         spline,
                         automator,
                         upsampledAutomationAlpha);

        break;

      case Topology::EmptySideChain:
        break;

      default:
        assert(false);
        break;
    }
  }

  // downsampling

  oversampling->vecToVecDownsamplers[0]->processBlock(
    upsampledBuffer, 2, numUpsampledSamples, numSamples);

  oversampling->vecToVecDownsamplers[1]->processBlock(
    upsampledDryBuffer, 2, numUpsampledSamples, numSamples);

  // dry-wet and output gain

  auto& wetOutput = oversampling->vecToVecDownsamplers[0]->getOutput();
  auto& dryOutput = oversampling->vecToVecDownsamplers[1]->getOutput();

  if (isWetPassNeeded) {

    auto& dryBuffer = dryOutput.getBuffer2(0);
    auto& wetBuffer = wetOutput.getBuffer2(0);

    Vec2d alpha = automationAlpha;

    Vec2d amount = Vec2d().load(wetAmount);
    Vec2d amountTarget = Vec2d().load(wetAmountTarget);

    Vec2d gain = Vec2d().load(outputGain);
    Vec2d gainTarget = Vec2d().load(outputGainTarget);

    for (int i = 0; i < numSamples; ++i) {
      amount = alpha * (amountTarget - amount) + amountTarget;
      gain = alpha * (gainTarget - gain) + gainTarget;
      Vec2d wet = gain * wetBuffer[i];
      Vec2d dry = dryBuffer[i];
      wetBuffer[i] = amount * (wet - dry) + dry;
    }

    amount.store(wetAmount);
    gain.store(outputGain);
  }
  else {
    if (!isBypassing) {

      auto& wetBuffer = wetOutput.getBuffer2(0);

      Vec2d alpha = automationAlpha;
      Vec2d gain = Vec2d().load(outputGain);
      Vec2d gainTarget = Vec2d().load(outputGainTarget);

      for (int i = 0; i < numSamples; ++i) {
        gain = alpha * (gainTarget - gain) + gainTarget;
        wetBuffer[i] = gain * wetBuffer[i];
      }

      gain.store(outputGain);
    }
  }

  if (isBypassing) {
    dryOutput.deinterleave(ioAudio, 2, numSamples);
  }
  else {
    wetOutput.deinterleave(ioAudio, 2, numSamples);
  }

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
