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
#include "Attachments.h"
#include "Linkables.h"
#include <JuceHeader.h>
#include <array>

struct GammaEnvParameters
{
  LinkableParameter<AudioParameterFloat> attack;
  LinkableParameter<AudioParameterFloat> release;
  LinkableParameter<AudioParameterFloat> attackDelay;
  LinkableParameter<AudioParameterFloat> releaseDelay;
  LinkableParameter<AudioParameterChoice> metric;
  AudioParameterFloat* stereoLink;
};

struct GammaEnvEditor
{
public:
  GammaEnvEditor(Component& owner,
                 AudioProcessorValueTreeState& apvts,
                 GammaEnvParameters& parameters,
                 String const& midSideParamID = "Mid-Side");

  virtual ~GammaEnvEditor();

  void resizeLinkables(Point<float> topLeft,
                       float width,
                       float height,
                       float columnGap = 2.f,
                       float rowGap = 2.f);

  ChannelLabels channelLabels;

  LinkableComboBox metric;
  LinkableControl<AttachedSlider> attack;
  LinkableControl<AttachedSlider> release;
  LinkableControl<AttachedSlider> attackDelay;
  LinkableControl<AttachedSlider> releaseDelay;
  AttachedSlider stereoLink;
  Label stereoLinkLabel{ {}, "Stereo Link" };

private:
  Component& owner;
};