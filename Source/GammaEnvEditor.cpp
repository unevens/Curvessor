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

#include "GammaEnvEditor.h"

GammaEnvEditor::GammaEnvEditor(Component& owner,
                               AudioProcessorValueTreeState& apvts,
                               GammaEnvParameters& parameters,
                               String const& midSideParamID)
  : attack(owner, apvts, "Attack", parameters.attack)
  , release(owner, apvts, "Release", parameters.release)
  , releaseDelay(owner, apvts, "Release Delay", parameters.releaseDelay)
  , attackDelay(owner, apvts, "Attack Delay", parameters.attackDelay)
  , metric(owner, apvts, "Metric", { "Peak", "RMS" }, parameters.metric)
  , stereoLink(owner, apvts, parameters.stereoLink->paramID)
  , channelLabels(owner, apvts, midSideParamID)
  , owner(owner)
{
  owner.addAndMakeVisible(stereoLinkLabel);
}

GammaEnvEditor::~GammaEnvEditor()
{
  owner.removeChildComponent(&stereoLinkLabel);
}

void
GammaEnvEditor::resizeLinkables(Point<float> topLeft,
                                float width,
                                float rowHeight,
                                float columnGap,
                                float rowGap)
{
  float const columnWidth = (width - 6.f * columnGap) / 6.f;
  float const gap = columnWidth + columnGap;

  channelLabels.resize(topLeft, columnWidth, rowHeight, rowGap);
  topLeft.x += gap;
  metric.resize(topLeft, columnWidth, rowHeight, rowGap);
  topLeft.x += gap;
  attack.resize(topLeft, columnWidth, rowHeight, rowGap);
  topLeft.x += gap;
  release.resize(topLeft, columnWidth, rowHeight, rowGap);
  topLeft.x += gap;
  attackDelay.resize(topLeft, columnWidth, rowHeight, rowGap);
  topLeft.x += gap;
  releaseDelay.resize(topLeft, columnWidth, rowHeight, rowGap);
  topLeft.x += gap;
}
