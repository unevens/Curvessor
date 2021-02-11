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

__m128d
applyHighPassFilter(__m128d input, __m128d& state, __m128d g)
{
  __m128d v = _mm_mul_pd(g, _mm_sub_pd(input, state));
  __m128d low = _mm_add_pd(v, state);
  state = _mm_add_pd(low, v);
  return _mm_sub_pd(input, low);
}

constexpr double ln10 = 2.30258509299404568402;
constexpr double db_to_lin = ln10 / 20.0;

template<int highPassOrder>
void
CurvessorAudioProcessor::Dsp::forwardProcess(VecBuffer<Vec2d>& io,
                                             int const numActiveKnots)
{
  auto spline = autoSpline.spline.getVecSpline<maxNumKnots>();
  auto automation = autoSpline.automator.getVecAutomator<maxNumKnots>();
  auto envelope = envelopeFollower.getVecData();

  __m128d stereo_link_target = _mm_set1_pd(stereoLinkTarget);
  __m128d automation_alpha = _mm_set1_pd(automationAlpha);

  __m128d stereo_link = _mm_load_pd(stereoLink);
  __m128d gain_vumeter = _mm_load_pd(gainVuMeterBuffer);
  __m128d level_vumeter = _mm_load_pd(levelVuMeterBuffer);

  __m128d high_pass_state = _mm_load_pd(highPassState);
  __m128d high_pass_coef = _mm_load_pd(highPassCoef);
  __m128d high_pass_state_2 = _mm_load_pd(highPassState2);
  __m128d high_pass_state_3 = _mm_load_pd(highPassState3);

  int const numSamples = io.getNumSamples();

  for (int i = 0; i < numSamples; ++i) {

    Vec2d in = io[i];

    Vec2d env_in = in;

    if constexpr (highPassOrder >= 1) {
      env_in = applyHighPassFilter(in, high_pass_state, high_pass_coef);
    }

    if constexpr (highPassOrder >= 2) {
      env_in = applyHighPassFilter(env_in, high_pass_state_2, high_pass_coef);
    }

    if constexpr (highPassOrder >= 3) {
      env_in = applyHighPassFilter(env_in, high_pass_state_3, high_pass_coef);
    }

    Vec2d env_out = envelope.processDB(env_in);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc = spline.process<SplineAutomator::VecAutomator>(
      env_out, automation, numActiveKnots);

    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[i] = in * gc;
  }

  autoSpline.spline.update(spline, numActiveKnots);
  envelope.update(envelopeFollower);
  _mm_store_pd(stereoLink, stereo_link);
  _mm_store_pd(gainVuMeterBuffer, gain_vumeter);
  _mm_store_pd(levelVuMeterBuffer, level_vumeter);
  _mm_store_pd(highPassState, high_pass_state);
  _mm_store_pd(highPassState2, high_pass_state_2);
  _mm_store_pd(highPassState3, high_pass_state_3);
}

template<int highPassOrder>
void
CurvessorAudioProcessor::Dsp::feedbackProcess(VecBuffer<Vec2d>& io,
                                              int const numActiveKnots)
{
  auto spline = autoSpline.spline.getVecSpline<maxNumKnots>();
  auto automation = autoSpline.automator.getVecAutomator<maxNumKnots>();

  auto envelope = envelopeFollower.getVecData();

  __m128d stereo_link_target = _mm_set1_pd(stereoLinkTarget);
  __m128d automation_alpha = _mm_set1_pd(automationAlpha);

  __m128d stereo_link = _mm_load_pd(stereoLink);
  __m128d gain_vumeter = _mm_load_pd(gainVuMeterBuffer);
  __m128d level_vumeter = _mm_load_pd(levelVuMeterBuffer);

  __m128d feedback_amount_target = _mm_load_pd(feedbackAmountTarget);
  __m128d feedback_amount = _mm_load_pd(feedbackAmount);
  __m128d env_in = _mm_load_pd(feedbackBuffer);

  __m128d high_pass_state = _mm_load_pd(highPassState);
  __m128d high_pass_coef = _mm_load_pd(highPassCoef);
  __m128d high_pass_state_2 = _mm_load_pd(highPassState2);
  __m128d high_pass_state_3 = _mm_load_pd(highPassState3);

  int const numSamples = io.getNumSamples();

  for (int s = 0; s < numSamples; ++s) {

    Vec2d in = io[s];

    feedback_amount =
      feedback_amount +
      automation_alpha * (feedback_amount_target - feedback_amount);

    env_in = in + feedback_amount * (env_in - in);

    if constexpr (highPassOrder >= 1) {
      env_in = applyHighPassFilter(env_in, high_pass_state, high_pass_coef);
    }

    if constexpr (highPassOrder >= 2) {
      env_in = applyHighPassFilter(env_in, high_pass_state_2, high_pass_coef);
    }

    Vec2d env_out = envelope.processDB(env_in);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc = spline.process<SplineAutomator::VecAutomator>(
      env_out, automation, numActiveKnots);

    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[s] = env_in = in * gc;
  }

  autoSpline.spline.update(spline, numActiveKnots);
  envelope.update(envelopeFollower);
  _mm_store_pd(stereoLink, stereo_link);
  _mm_store_pd(gainVuMeterBuffer, gain_vumeter);
  _mm_store_pd(levelVuMeterBuffer, level_vumeter);
  _mm_store_pd(feedbackBuffer, env_in);
  _mm_store_pd(feedbackAmount, feedback_amount);
  _mm_store_pd(highPassState, high_pass_state);
  _mm_store_pd(highPassState2, high_pass_state_2);
}

template<int highPassOrder>
void
CurvessorAudioProcessor::Dsp::sidechainProcess(VecBuffer<Vec2d>& io,
                                               VecBuffer<Vec2d>& sidechain,
                                               int const numActiveKnots)
{
  auto spline = autoSpline.spline.getVecSpline<maxNumKnots>();
  auto automation = autoSpline.automator.getVecAutomator<maxNumKnots>();

  auto envelope = envelopeFollower.getVecData();

  __m128d automation_alpha = _mm_set1_pd(automationAlpha);
  __m128d stereo_link_target = _mm_set1_pd(stereoLinkTarget);

  __m128d stereo_link = _mm_load_pd(stereoLink);
  __m128d gain_vumeter = _mm_load_pd(gainVuMeterBuffer);
  __m128d level_vumeter = _mm_load_pd(levelVuMeterBuffer);

  __m128d high_pass_state = _mm_load_pd(highPassState);
  __m128d high_pass_coef = _mm_load_pd(highPassCoef);
  __m128d high_pass_state_2 = _mm_load_pd(highPassState2);
  __m128d high_pass_state_3 = _mm_load_pd(highPassState3);

  int const numSamples = io.getNumSamples();

  for (int s = 0; s < numSamples; ++s) {
    Vec2d in = io[s];

    Vec2d env_in = sidechain[s];

    if constexpr (highPassOrder >= 1) {
      env_in = applyHighPassFilter(env_in, high_pass_state, high_pass_coef);
    }

    if constexpr (highPassOrder >= 2) {
      env_in = applyHighPassFilter(env_in, high_pass_state_2, high_pass_coef);
    }

    if constexpr (highPassOrder >= 3) {
      env_in = applyHighPassFilter(env_in, high_pass_state_3, high_pass_coef);
    }

    Vec2d env_out = envelope.processDB(env_in);

    env_out = applyStereoLink(
      env_out, stereo_link, stereo_link_target, automation_alpha);

    level_vumeter = toVumeter(level_vumeter, env_out, automation_alpha);

    Vec2d gc = spline.process<SplineAutomator::VecAutomator>(
      env_out, automation, numActiveKnots);

    gc -= env_out;

    gain_vumeter = toVumeter(gain_vumeter, gc, automation_alpha);

    gc = exp(db_to_lin * gc);

    io[s] = in * gc;
  }

  autoSpline.spline.update(spline, numActiveKnots);
  envelope.update(envelopeFollower);
  _mm_store_pd(stereoLink, stereo_link);
  _mm_store_pd(gainVuMeterBuffer, gain_vumeter);
  _mm_store_pd(levelVuMeterBuffer, level_vumeter);
  _mm_store_pd(highPassState, high_pass_state);
  _mm_store_pd(highPassState2, high_pass_state_2);
  _mm_store_pd(highPassState3, high_pass_state_3);
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

  // update settings from parameters

  bool const isMidSideEnabled = parameters.midSide->get();

  bool const isSideChainAvailable = totalNumInputChannels == 4;

  bool const isSideChainRequested = parameters.sideChain->get();

  bool const isUsingSideChain = isSideChainRequested && isSideChainAvailable;

  dsp->stereoLinkTarget = 0.01 * parameters.stereoLink->get();

  double const invUpsampledSampleRate =
    1.0 / (getSampleRate() * oversampling->getRate());

  double const bltFrequencyCoef =
    MathConstants<double>::pi * invUpsampledSampleRate;

  double const upsampledAngularFrequencyCoef =
    1000.0 * MathConstants<double>::twoPi * invUpsampledSampleRate;

  float const smoothingTime = parameters.smoothingTime->get();

  double const automationAlpha =
    smoothingTime == 0.f ? 0.f
                         : exp(-upsampledAngularFrequencyCoef / smoothingTime);

  double const upsampledAutomationAlpha =
    smoothingTime == 0.f ? 0.f
                         : exp(-upsampledAngularFrequencyCoef / smoothingTime);

  dsp->automationAlpha = upsampledAutomationAlpha;

  double inputGainTarget[2];
  double outputGainTarget[2];
  double wetAmountTarget[2];

  for (int c = 0; c < 2; ++c) {

    outputGainTarget[c] = exp(db_to_lin * parameters.outputGain.get(c)->get());
    inputGainTarget[c] = exp(db_to_lin * parameters.inputGain.get(c)->get());

    wetAmountTarget[c] = 0.01 * parameters.wet.get(c)->get();
    dsp->feedbackAmountTarget[c] =
      0.01 * parameters.feedbackAmount.get(c)->get();

    dsp->highPassCoef[c] = [&] {
      double const g =
        tan(bltFrequencyCoef * parameters.highPassCutoff.get(c)->get());
      return g / (1.0 + g);
    }();

    // evenlope follower settings

    float const rmsTime = parameters.envelopeFollower.rmsTime.get(c)->get();

    bool const rmsAlpha =
      rmsTime == 0.f ? 0.f : exp(-upsampledAngularFrequencyCoef / rmsTime);

    double const attackFrequency =
      parameters.envelopeFollower.attack.get(c)->get();

    double const releaseFrequency =
      upsampledAngularFrequencyCoef /
      parameters.envelopeFollower.release.get(c)->get();

    double const attackDelay =
      0.01 * parameters.envelopeFollower.attackDelay.get(c)->get();

    double const releaseDelay =
      0.01 * parameters.envelopeFollower.releaseDelay.get(c)->get();

    envelopeFollowerSettings.setup(c,
                                   rmsAlpha,
                                   attackFrequency,
                                   releaseFrequency,
                                   attackDelay,
                                   releaseDelay);
  }

  dsp->autoSpline.automator.setSmoothingAlpha(upsampledAutomationAlpha);

  int numActiveKnots = parameters.spline->updateSpline(dsp->autoSpline);

  bool const isWetPassNeeded = [&] {
    double m = wetAmountTarget[0] * wetAmountTarget[1] * dsp->wetAmount[0] *
               dsp->wetAmount[1];
    if (m == 1.0) {
      return false;
    }
    if (m == 0.0) {
      return !(wetAmountTarget[0] == 0.0 && wetAmountTarget[1] == 0.0 &&
               dsp->wetAmount[0] == 0.0 && dsp->wetAmount[1] == 0.0);
    }
    return true;
  }();

  bool const isBypassing =
    (!isWetPassNeeded && (dsp->wetAmount[0] == 0.0)) || (numActiveKnots == 0);

  int const highPassOrder = parameters.highPassOrder->getIndex();

  // ready to process

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

  applyGain(
    ioAudio, inputGainTarget, dsp->inputGain, automationAlpha, numSamples);

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

    applyGain(envelopeInput,
              inputGainTarget,
              dsp->sidechainInputGain,
              automationAlpha,
              numSamples);
  }

  oversampling->scalarToVecUpsamplers[2]->processBlock(
    envelopeInput, 2, numSamples);

  auto& upsampledSideChainInput =
    oversampling->scalarToVecUpsamplers[2]->getOutput().getBuffer2(0);

  // processing

  const bool isFeedbackNeeded = [&] {
    for (int c = 0; c < 2; ++c) {
      if (dsp->feedbackAmountTarget[c] > 0.f) {
        return true;
      }
      if (dsp->feedbackAmount[c] > 0.f) {
        return true;
      }
    }
    return false;
  }();

  if (!isBypassing) {
    if (isSideChainRequested) {
      if (isSideChainAvailable) {
        switch (highPassOrder) {
          case 0:
            dsp->sidechainProcess<0>(
              upsampledIo, upsampledSideChainInput, numActiveKnots);
            break;
          case 1:
            dsp->sidechainProcess<1>(
              upsampledIo, upsampledSideChainInput, numActiveKnots);
            break;
          case 2:
            dsp->sidechainProcess<2>(
              upsampledIo, upsampledSideChainInput, numActiveKnots);
            break;
          case 3:
            dsp->sidechainProcess<3>(
              upsampledIo, upsampledSideChainInput, numActiveKnots);
            break;
          default:
            assert(false);
            break;
        }
      }
    }
    else if (isFeedbackNeeded) {
      switch (highPassOrder) {
        case 0:
          dsp->feedbackProcess<0>(upsampledIo, numActiveKnots);
          break;
        case 1:
          dsp->feedbackProcess<1>(upsampledIo, numActiveKnots);
          break;
        case 2:
          dsp->feedbackProcess<2>(upsampledIo, numActiveKnots);
          break;
        case 3:
          dsp->feedbackProcess<3>(upsampledIo, numActiveKnots);
          break;
        default:
          assert(false);
          break;
      }
    }
    else {
      switch (highPassOrder) {
        case 0:
          dsp->forwardProcess<0>(upsampledIo, numActiveKnots);
          break;
        case 1:
          dsp->forwardProcess<1>(upsampledIo, numActiveKnots);
          break;
        case 2:
          dsp->forwardProcess<2>(upsampledIo, numActiveKnots);
          break;
        case 3:
          dsp->forwardProcess<3>(upsampledIo, numActiveKnots);
          break;
        default:
          assert(false);
          break;
      }
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

    Vec2d amount = Vec2d().load_a(dsp->wetAmount);
    Vec2d amountTarget = Vec2d().load(wetAmountTarget);

    Vec2d gain = Vec2d().load_a(dsp->outputGain);
    Vec2d gainTarget = Vec2d().load(outputGainTarget);

    for (int i = 0; i < numSamples; ++i) {
      amount = alpha * (amountTarget - amount) + amountTarget;
      gain = alpha * (gainTarget - gain) + gainTarget;
      Vec2d wet = gain * wetBuffer[i];
      Vec2d dry = dryBuffer[i];
      wetBuffer[i] = amount * (wet - dry) + dry;
    }

    amount.store(dsp->wetAmount);
    gain.store(dsp->outputGain);
  }
  else {
    if (!isBypassing) {

      auto& wetBuffer = wetOutput.getBuffer2(0);

      Vec2d alpha = automationAlpha;
      Vec2d gain = Vec2d().load(dsp->outputGain);
      Vec2d gainTarget = Vec2d().load(outputGainTarget);

      for (int i = 0; i < numSamples; ++i) {
        gain = alpha * (gainTarget - gain) + gainTarget;
        wetBuffer[i] = gain * wetBuffer[i];
      }

      gain.store(dsp->outputGain);
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
    levelVuMeterResults[i].store((float)dsp->levelVuMeterBuffer[i]);
    gainVuMeterResults[i].store((float)dsp->gainVuMeterBuffer[i]);
  }
}
