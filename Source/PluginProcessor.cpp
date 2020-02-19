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
  std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

  auto const CreateFloatParameter =
    [&](String name, float value, float min, float max, float step = 0.01f) {
      parameters.push_back(std::unique_ptr<RangedAudioParameter>(
        new AudioParameterFloat(name, name, { min, max, step }, value)));

      return static_cast<AudioParameterFloat*>(parameters.back().get());
    };

  auto const createWrappedBoolParameter = [&](String name, float value) {
    WrappedBoolParameter wrapper;
    parameters.push_back(wrapper.createParameter(name, value));
    return wrapper;
  };

  auto const CreateBoolParameter = [&](String name, float value) {
    parameters.push_back(std::unique_ptr<RangedAudioParameter>(
      new AudioParameterBool(name, name, value)));

    return static_cast<AudioParameterBool*>(parameters.back().get());
  };

  auto const CreateChoiceParameter =
    [&](String name, StringArray choices, int defaultIndex = 0) {
      parameters.push_back(std::unique_ptr<RangedAudioParameter>(
        new AudioParameterChoice(name, name, choices, defaultIndex)));

      return static_cast<AudioParameterChoice*>(parameters.back().get());
    };

  String const ch0Suffix = "_ch0";
  String const ch1Suffix = "_ch1";
  String const linkSuffix = "_is_linked";

  auto const CreateLinkableFloatParameters =
    [&](String name, float value, float min, float max, float step = 0.01f) {
      return LinkableParameter<AudioParameterFloat>{
        createWrappedBoolParameter(name + linkSuffix, true),
        { CreateFloatParameter(name + ch0Suffix, value, min, max, step),
          CreateFloatParameter(name + ch1Suffix, value, min, max, step) }
      };
    };

  auto const CreateLinkableChoiceParameters =
    [&](String name, StringArray choices, int defaultIndex = 0) {
      return LinkableParameter<AudioParameterChoice>{
        createWrappedBoolParameter(name + linkSuffix, true),
        { CreateChoiceParameter(name + ch0Suffix, choices, defaultIndex),
          CreateChoiceParameter(name + ch1Suffix, choices, defaultIndex) }
      };
    };

  midSide = CreateBoolParameter("Mid-Side", false);

  topology =
    CreateChoiceParameter("Topology", { "Forward", "Feedback", "SideChain" });

  inputGain = CreateLinkableFloatParameters("Input-Gain", 0.f, -48.f, +48.f);

  outputGain = CreateLinkableFloatParameters("Output-Gain", 0.f, -48.f, +48.f);

  envelopeFollower.attack =
    CreateLinkableFloatParameters("Attack", 20.0, 0.1, 2000.0);

  envelopeFollower.release =
    CreateLinkableFloatParameters("Release", 200.0, 1.0, 2000.0);

  envelopeFollower.attackDelay =
    CreateLinkableFloatParameters("Attack-Delay", 0.0, 0.0, 25.0);

  envelopeFollower.releaseDelay =
    CreateLinkableFloatParameters("Release-Delay", 0.0, 0.0, 25.0);

  envelopeFollower.metric =
    CreateLinkableChoiceParameters("Metric", { "Peak", "RMS" });

  stereoLink =
    CreateFloatParameter("Stereo-Link", 50.0, 0.0, 100.0);

  spline = std::unique_ptr<SplineParameters>(
    new SplineParameters("Spline",
                         parameters,
                         CurvessorAudioProcessor::maxNumNodes,
                         { -100.f, 6.f, 0.01f },
                         { -100.f, 6.f, 0.01f },
                         { -20.f, 20.f, 0.01f }));

  apvts = std::unique_ptr<AudioProcessorValueTreeState>(
    new AudioProcessorValueTreeState(processor,
                                     nullptr,
                                     "CURVESSOR-PARAMETERS",
                                     { parameters.begin(), parameters.end() }));
}

CurvessorAudioProcessor::CurvessorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
  : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
                     .withInput("Input", AudioChannelSet::stereo(), true)
#endif
                     .withOutput("Output", AudioChannelSet::stereo(), true)
#endif
                     )
#endif
  , parameters(*this)

  , envelopeFollower(Aligned<avec::GammaEnv<Vec2d>>::New())

  , splines(avec::SplineHolder<avec::Spline, Vec2d>::New<maxNumNodes>())

  , envelopeFollowerSettings(*envelopeFollower)

  , asyncOversampling([this] {
    auto oversampling = OversamplingSettings{};
    oversampling.numScalarToVecUpsamplers = 2;
    oversampling.numVecToScalarDownsamplers = 1;
    oversampling.numChannels = 2;
    oversampling.UpdateLatency = [this](int latency) {
      setLatencySamples(latency);
    };
    return oversampling;
  }())

  , oversamplingGetter(
      *oversimple::RequestOversamplingGetter<double>(asyncOversampling))

  , oversamplingSerializationGetter(
      *oversimple::RequestOversamplingSettingsGetter(asyncOversampling))

  , oversamplingGuiGetter(
      *oversimple::RequestOversamplingSettingsGetter(asyncOversampling))

  , oversamplingAwaiter(asyncOversampling.requestAwaiter())

{
  levelVuMeterResults[0].store(-500.f);
  levelVuMeterResults[1].store(-500.f);
  gainVuMeterResults[0].store(0.f);
  gainVuMeterResults[1].store(0.f);

  asyncOversampling.startTimer();
}

void
CurvessorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  asyncOversampling.submitMessage([=](OversamplingSettings& oversampling) {
    oversampling.numSamplesPerBlock = samplesPerBlock;
  });

  floatToDouble = AudioBuffer<double>(4, samplesPerBlock);

  reset();

  oversamplingAwaiter.await();
}

void
CurvessorAudioProcessor::reset()
{
  envelopeFollower->Reset();
  splines.Reset();
  levelVuMeterBuffer[0] = -200.0;
  gainVuMeterBuffer[0] = 0.f;
  stereoLink[0] = stereoLinkTarget[0];

  constexpr double ln10 = 2.30258509299404568402;
  constexpr double db_to_lin = ln10 / 20.0;

  for (int c = 0; c < 2; ++c) {
    inputGain[c] = exp(db_to_lin * parameters.inputGain.get(c)->get());
    outputGain[c] = exp(db_to_lin * parameters.outputGain.get(c)->get());
    sidechainInputGain[c] = inputGain[c];
  }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool
CurvessorAudioProcessor::isBusesLayoutSupported(
  const BusesLayout& layouts) const
{
  if (layouts.getMainOutputChannels() != 2) {
    return false;
  }
  if (layouts.getMainInputChannels() == 2 ||
      layouts.getMainInputChannels() == 4) {
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
  oversamplingSerializationGetter.update();
  auto& oversampling = oversamplingSerializationGetter.get();

  auto oversamplingData = MemoryBlock(2 * sizeof(int));
  ((int*)oversamplingData.getData())[0] = oversampling.order;
  ((int*)oversamplingData.getData())[1] = oversampling.linearPhase;

  auto state = parameters.apvts->copyState();
  std::unique_ptr<XmlElement> xml(state.createXml());
  auto paramData = MemoryBlock();
  copyXmlToBinary(*xml, paramData);
  destData.append(oversamplingData.getData(), oversamplingData.getSize());
  destData.append(paramData.getData(), paramData.getSize());
}

void
CurvessorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  int* oversamplingData = (int*)data;
  int order = oversamplingData[0];
  bool linearPhase = oversamplingData[1];
  asyncOversampling.submitMessage([=](OversamplingSettings& oversampling) {
    oversampling.order = order;
    oversampling.linearPhase = linearPhase;
  });

  void* paramData = (void*)((int*)data + 2);

  std::unique_ptr<XmlElement> xmlState(
    getXmlFromBinary(paramData, sizeInBytes - 2 * sizeof(int)));

  if (xmlState.get() != nullptr) {
    if (xmlState->hasTagName(parameters.apvts->state.getType())) {
      parameters.apvts->replaceState(ValueTree::fromXml(*xmlState));
    }
  }

  oversamplingAwaiter.await();
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
