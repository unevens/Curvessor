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

#pragma once

#include "GammaEnvEditor.h"
#include "Linkables.h"
#include "OversamplingParameters.h"
#include "SimpleLookAndFeel.h"
#include "SplineParameters.h"
#include "adsp/GammaEnv.hpp"
#include "adsp/Spline.hpp"
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
  static constexpr int maxNumKnots = 9;
  static constexpr int maxEditableKnots = maxNumKnots - 1;

  enum class Topology
  {
    Forward,
    Feedback,
    SideChain,
    EmptySideChain,
  };

private:
  struct Parameters
  {
    AudioParameterBool* midSide;
    AudioParameterChoice* topology;
    LinkableParameter<AudioParameterFloat> inputGain;
    LinkableParameter<AudioParameterFloat> outputGain;
    LinkableParameter<AudioParameterFloat> wet;
    GammaEnvParameters envelopeFollower;
    AudioParameterFloat* stereoLink;
    AudioParameterFloat* smoothingTime;
    OversamplingParameters oversampling;

    std::unique_ptr<SplineParameters> spline;

    std::unique_ptr<AudioProcessorValueTreeState> apvts;

    Parameters(CurvessorAudioProcessor& processor);
  };

  Parameters parameters;

  using Spline = adsp::Spline<Vec2d, maxNumKnots>;
  using AutoSpline = adsp::AutoSpline<Vec2d, maxNumKnots>;
  using SplineAutomator = adsp::Spline<Vec2d, maxNumKnots>::SmoothingAutomator;

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
    double levelVuMeterBuffer[2];
    double gainVuMeterBuffer[2];

    Dsp()
    {
      AVEC_ASSERT_ALIGNMENT(this, Vec2d);
      std::fill_n(stereoLink, 2 * 8, 0.0);
    }

    void reset(Parameters& parameters);

    void forwardProcess(VecBuffer<Vec2d>& io,
                        int const numActiveKnots,
                        double const automationAlpha,
                        double const stereoLinkTarget);

    void feedbackProcess(VecBuffer<Vec2d>& io,
                         int const numActiveKnots,
                         double const automationAlpha,
                         double const stereoLinkTarget);

    void sidechainProcess(VecBuffer<Vec2d>& io,
                          VecBuffer<Vec2d>& sidechain,
                          int const numActiveKnots,
                          double const automationAlpha,
                          double const stereoLinkTarget);
  };

  aligned_ptr<Dsp> dsp;

  adsp::GammaEnvSettings<Vec2d> envelopeFollowerSettings;

  ScalarBuffer<double> dryBuffer{ 2 };

  // buffer for single precision processing call
  AudioBuffer<double> floatToDouble;

  // oversampling
  using Oversampling = oversimple::Oversampling<double>;
  using OversamplingSettings = oversimple::OversamplingSettings;

  OversamplingSettings oversamplingSettings;
  std::unique_ptr<Oversampling> oversampling;
  std::recursive_mutex oversamplingMutex;
  OversamplingAttachments<double, std::recursive_mutex> oversamplingAttachments;

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
  void reset() override { dsp->reset(parameters); }

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
