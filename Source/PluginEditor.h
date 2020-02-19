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

#include "GainVuMeter.h"
#include "GammaEnvEditor.h"
#include "PluginProcessor.h"
#include "SplineEditor.h"
#include <JuceHeader.h>

class CurvessorAudioProcessorEditor
  : public AudioProcessorEditor
  , public Timer
{
public:
  CurvessorAudioProcessorEditor(CurvessorAudioProcessor&);
  ~CurvessorAudioProcessorEditor();

  void paint(Graphics&) override;
  void resized() override;

private:
  CurvessorAudioProcessor& processor;

  SplineEditor splineEditor;
  SplineNodeEditor nodeEditor;
  GammaEnvEditor gammaEnvEditor;
  GainVuMeter vuMeter;
  AttachedToggle midSideEditor;
  AttachedComboBox topologyEditor;
  Label topologyLabel{ {}, "Topology" };
  ToggleButton linearPhase;
  ComboBox oversampling;
  Label oversamplingLabel{ {}, "Oversampling" };
  AttachedSlider stereoLink;
  Label stereoLinkLabel{ {}, "Stereo Link" };
  LinkableControl<AttachedSlider> inputGain;
  LinkableControl<AttachedSlider> outputGain;
  void timerCallback() override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurvessorAudioProcessorEditor)
};
