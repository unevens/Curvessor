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
#include "PluginEditor.h"

CurvessorAudioProcessor::Parameters::Parameters(
  CurvessorAudioProcessor& processor)
{
  AudioProcessorValueTreeState::ParameterLayout layout;

  auto const createFloatParameter =
    [&](String name, float value, float min, float max, float step = 0.01f) {
      auto p = new AudioParameterFloat(name, name, { min, max, step }, value);
      layout.add(std::unique_ptr<RangedAudioParameter>(p));
      return static_cast<AudioParameterFloat*>(p);
    };

  auto const createWrappedBoolParameter = [&](String name, float value) {
    WrappedBoolParameter wrapper;
    layout.add(wrapper.createParameter(name, value));
    return wrapper;
  };

  auto const createBoolParameter = [&](String name, float value) {
    auto p = new AudioParameterBool(name, name, value);
    layout.add(std::unique_ptr<RangedAudioParameter>(p));
    return static_cast<AudioParameterBool*>(p);
  };

  auto const createChoiceParameter =
    [&](String name, StringArray choices, int defaultIndex = 0) {
      auto p = new AudioParameterChoice(name, name, choices, defaultIndex);
      layout.add(std::unique_ptr<RangedAudioParameter>(p));
      return static_cast<AudioParameterChoice*>(p);
    };

  String const ch0Suffix = "_ch0";
  String const ch1Suffix = "_ch1";
  String const linkSuffix = "_is_linked";

  auto const createLinkableFloatParameters =
    [&](String name, float value, float min, float max, float step = 0.01f) {
      return LinkableParameter<AudioParameterFloat>{
        createWrappedBoolParameter(name + linkSuffix, true),
        { createFloatParameter(name + ch0Suffix, value, min, max, step),
          createFloatParameter(name + ch1Suffix, value, min, max, step) }
      };
    };

  auto const createLinkableChoiceParameters =
    [&](String name, StringArray choices, int defaultIndex = 0) {
      return LinkableParameter<AudioParameterChoice>{
        createWrappedBoolParameter(name + linkSuffix, true),
        { createChoiceParameter(name + ch0Suffix, choices, defaultIndex),
          createChoiceParameter(name + ch1Suffix, choices, defaultIndex) }
      };
    };

  midSide = createBoolParameter("Mid-Side", false);

  smoothingTime = createFloatParameter("Smoothing-Time", 50.f, 0.f, 500.f, 1.f);

  topology =
    createChoiceParameter("Topology", { "Forward", "Feedback", "SideChain" });

  oversampling = { static_cast<RangedAudioParameter*>(createChoiceParameter(
                     "Oversampling", { "1x", "2x", "4x", "8x", "16x", "32x" })),
                   createWrappedBoolParameter("Linear-Phase-Oversampling",
                                              false) };

  inputGain = createLinkableFloatParameters("Input-Gain", 0.f, -48.f, 48.f);

  outputGain = createLinkableFloatParameters("Output-Gain", 0.f, -48.f, 48.f);

  wet = createLinkableFloatParameters("Wet", 100.f, 0.f, 100.f, 1.f);

  envelopeFollower.attack =
    createLinkableFloatParameters("Attack", 20.f, 0.1f, 2000.f);

  envelopeFollower.release =
    createLinkableFloatParameters("Release", 200.f, 1.f, 2000.f);

  envelopeFollower.attackDelay =
    createLinkableFloatParameters("Attack-Delay", 0.f, 0.f, 25.f);

  envelopeFollower.releaseDelay =
    createLinkableFloatParameters("Release-Delay", 0.f, 0.f, 25.f);

  envelopeFollower.metric =
    createLinkableChoiceParameters("Metric", { "Peak", "RMS" });

  stereoLink = createFloatParameter("Stereo-Link", 50.f, 0.f, 100.f, 1.f);

  auto const isKnotActive = [](int knotIndex) {
    return knotIndex >= 3 && knotIndex <= 6;
  };

  spline = std::unique_ptr<SplineParameters>(
    new SplineParameters("",
                         layout,
                         CurvessorAudioProcessor::maxEditableKnots,
                         { -96.f, 6.f, 0.01f },
                         { -96.f, 6.f, 0.01f },
                         { -20.f, 20.f, 0.01f },
                         isKnotActive,
                         { { -96.f, -96.f, 1.f, 1.f } }));

  apvts = std::unique_ptr<AudioProcessorValueTreeState>(
    new AudioProcessorValueTreeState(
      processor, nullptr, "CURVESSOR-PARAMETERS", std::move(layout)));
}

CurvessorAudioProcessor::CurvessorAudioProcessor()

#ifndef JucePlugin_PreferredChannelConfigurations
  : AudioProcessor(BusesProperties()
                     .withInput("Input", AudioChannelSet::stereo(), true)
                     .withOutput("Output", AudioChannelSet::stereo(), true)
                     .withInput("Sidechain", AudioChannelSet::stereo()))
#endif

  , parameters(*this)

  , dsp(Aligned<Dsp>::make())

  , envelopeFollowerSettings(dsp->envelopeFollower)

  , oversamplingSettings([this] {
    auto oversamplingSettings = OversamplingSettings{};
    oversamplingSettings.numScalarToVecUpsamplers = 3;
    oversamplingSettings.numVecToVecDownsamplers = 2;
    oversamplingSettings.numChannels = 2;
    oversamplingSettings.updateLatency = [this](int latency) {
      setLatencySamples(latency);
    };
    return oversamplingSettings;
  }())

  , oversampling(std::make_unique<Oversampling>(oversamplingSettings))

  , oversamplingAttachments(parameters.oversampling,
                            *parameters.apvts,
                            this,
                            &oversampling,
                            &oversamplingSettings,
                            &oversamplingMutex)
{
  levelVuMeterResults[0].store(-500.f);
  levelVuMeterResults[1].store(-500.f);
  gainVuMeterResults[0].store(0.f);
  gainVuMeterResults[1].store(0.f);

  looks.simpleFontSize *= uiGlobalScaleFactor;
  looks.simpleSliderLabelFontSize *= uiGlobalScaleFactor;
  looks.simpleRotarySliderOffset *= uiGlobalScaleFactor;

  LookAndFeel::setDefaultLookAndFeel(&looks);
}

void
CurvessorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  dryBuffer.setNumSamples(samplesPerBlock);

  floatToDouble = AudioBuffer<double>(4, samplesPerBlock);

  if (oversamplingSettings.numSamplesPerBlock != samplesPerBlock) {
    auto const guard = std::lock_guard<std::recursive_mutex>(oversamplingMutex);
    oversamplingSettings.numSamplesPerBlock = samplesPerBlock;
    oversampling = std::make_unique<Oversampling>(oversamplingSettings);
  }

  reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool
CurvessorAudioProcessor::isBusesLayoutSupported(
  const BusesLayout& layouts) const
{
  if (layouts.getMainOutputChannels() != 2) {
    return false;
  }
  if (layouts.getMainInputChannels() != 2) {
    return false;
  }
  int const sidechainChannels = layouts.getNumChannels(true, 1);
  if (sidechainChannels == 2 || sidechainChannels == 0) {
    return true;
  }
  return false;
}
#endif

void
CurvessorAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                      MidiBuffer& midiMessages)
{
  auto totalNumInputChannels = getTotalNumInputChannels();

  for (int c = 0; c < totalNumInputChannels; ++c) {
    std::copy(buffer.getReadPointer(c),
              buffer.getReadPointer(c) + buffer.getNumSamples(),
              floatToDouble.getWritePointer(c));
  }

  for (int c = totalNumInputChannels; c < 4; ++c) {
    floatToDouble.clear(c, 0, floatToDouble.getNumSamples());
  }

  processBlock(floatToDouble, midiMessages);

  for (int c = 0; c < totalNumInputChannels; ++c) {
    std::copy(floatToDouble.getReadPointer(c),
              floatToDouble.getReadPointer(c) + floatToDouble.getNumSamples(),
              buffer.getWritePointer(c));
  }
}

void
CurvessorAudioProcessor::getStateInformation(MemoryBlock& destData)
{
  auto state = parameters.apvts->copyState();
  std::unique_ptr<XmlElement> xml(state.createXml());
  copyXmlToBinary(*xml, destData);
}

void
CurvessorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

  if (xmlState.get() != nullptr) {
    if (xmlState->hasTagName(parameters.apvts->state.getType())) {
      parameters.apvts->replaceState(ValueTree::fromXml(*xmlState));
    }
  }
}

void
CurvessorAudioProcessor::releaseResources()
{
  floatToDouble = AudioBuffer<double>(0, 0);
}

//==============================================================================
CurvessorAudioProcessor::~CurvessorAudioProcessor() {}

const String
CurvessorAudioProcessor::getName() const
{
  return JucePlugin_Name;
}

bool
CurvessorAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool
CurvessorAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool
CurvessorAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double
CurvessorAudioProcessor::getTailLengthSeconds() const
{
  return 0.0;
}

int
CurvessorAudioProcessor::getNumPrograms()
{
  return 1; // NB: some hosts don't cope very well if you tell them there are 0
            // programs, so this should be at least 1, even if you're not really
            // implementing programs.
}

int
CurvessorAudioProcessor::getCurrentProgram()
{
  return 0;
}

void
CurvessorAudioProcessor::setCurrentProgram(int index)
{}

const String
CurvessorAudioProcessor::getProgramName(int index)
{
  return {};
}

void
CurvessorAudioProcessor::changeProgramName(int index, const String& newName)
{}

bool
CurvessorAudioProcessor::hasEditor() const
{
  return true;
}

AudioProcessorEditor*
CurvessorAudioProcessor::createEditor()
{
  return new CurvessorAudioProcessorEditor(*this);
}

AudioProcessor* JUCE_CALLTYPE
createPluginFilter()
{
  return new CurvessorAudioProcessor();
}

void
CurvessorAudioProcessor::Dsp::reset(Parameters& parameters)
{
  parameters.spline->updateSpline(autoSpline);

  envelopeFollower.reset();
  autoSpline.reset();

  constexpr double ln10 = 2.30258509299404568402;
  constexpr double db_to_lin = ln10 / 20.0;

  double const stereoLinkTarget = 0.01 * parameters.stereoLink->get();

  for (int c = 0; c < 2; ++c) {
    gainVuMeterBuffer[c] = 0.f;
    levelVuMeterBuffer[c] = -200.0;
    stereoLink[c] = stereoLinkTarget;
    inputGain[c] = exp(db_to_lin * parameters.inputGain.get(c)->get());
    outputGain[c] = exp(db_to_lin * parameters.outputGain.get(c)->get());
    wetAmount[c] = 0.01 * parameters.wet.get(c)->get();
    sidechainInputGain[c] = inputGain[c];
  }
}
