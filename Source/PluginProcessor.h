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

#include "CurvessorDsp.h"
#include "GammaEnvEditor.h"
#include "OversamplingAttachments.h"
#include "Linkables.h"
#include "SimpleLookAndFeel.h"
#include "SplineParameters.h"
#include "avec/Buffer.hpp"
#include <JuceHeader.h>

#ifndef CURVESSOR_UI_SCALE
#define CURVESSOR_UI_SCALE 0.8f
#endif

static constexpr float uiGlobalScaleFactor = CURVESSOR_UI_SCALE;

constexpr static long double operator"" _p(long double px)
{
  return px * uiGlobalScaleFactor;
}

class CurvessorAudioProcessor : public AudioProcessor
{
public:
  static constexpr int maxNumKnots = curvessor::maxNumKnots;
  static constexpr int maxEditableKnots = curvessor::maxEditableKnots;

private:
  struct Parameters
  {
    AudioParameterBool* midSide;
    AudioParameterBool* sideChain;
    LinkableParameter<AudioParameterFloat> inputGain;
    LinkableParameter<AudioParameterFloat> outputGain;
    LinkableParameter<AudioParameterFloat> wet;
    LinkableParameter<AudioParameterFloat> feedbackAmount;
    LinkableParameter<AudioParameterFloat> rmsTime;
    GammaEnvParameters envelopeFollower;
    AudioParameterFloat* stereoLink;
    AudioParameterFloat* smoothingTime;
    OversamplingParameters oversampling;
    LinkableParameter<AudioParameterFloat> highPassCutoff;
    AudioParameterChoice* highPassOrder;

    std::unique_ptr<SplineParameters> spline;

    std::unique_ptr<AudioProcessorValueTreeState> apvts;

    Parameters(CurvessorAudioProcessor& processor);
  };

  Parameters parameters;

  aligned_ptr<curvessor::Dsp> dsp;

  void resetDsp();

  adsp::GammaEnvSettings<Vec2d> envelopeFollowerSettings;

  avec::Buffer<double> dryBuffer{ 2 };

  // buffer for single precision processing call
  AudioBuffer<double> floatToDouble;

  // oversampling
  oversimple::OversamplingSettings oversamplingSettings;
  oversimple::TOversampling<double> wetOversampling;
  oversimple::TOversampling<double> dryOversampling;
  oversimple::TOversampling<double> sidechainOversampling;
  std::recursive_mutex oversamplingMutex;
  OversamplingAttachments<std::recursive_mutex> oversamplingAttachments;

public:
  // for gui

  SimpleLookAndFeel looks;

  Parameters& getCurvessorParameters() { return parameters; }

  std::array<std::atomic<float>, 2> levelVuMeterResults;
  std::array<std::atomic<float>, 2> gainVuMeterResults;

  // AudioProcessor interface

  //==============================================================================
  CurvessorAudioProcessor();
  ~CurvessorAudioProcessor();

  //==============================================================================
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void reset() override { resetDsp(); }

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

  void processBlock(AudioBuffer<float>&, MidiBuffer&) override;

  bool supportsDoublePrecisionProcessing() const override { return true; }

  void processBlock(AudioBuffer<double>& buffer, MidiBuffer& midi) override;

  //==============================================================================
  AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  //==============================================================================
  const String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  //==============================================================================
  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const String getProgramName(int index) override;
  void changeProgramName(int index, const String& newName) override;

  //==============================================================================
  void getStateInformation(MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

private:
  //==============================================================================
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurvessorAudioProcessor)
};
