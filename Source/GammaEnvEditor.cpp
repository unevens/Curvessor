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

GammaEnvEditor::GammaEnvEditor(AudioProcessorValueTreeState& apvts,
                               GammaEnvParameters& parameters,
                               String const& midSideParamID)
  : attack(apvts, "Attack", parameters.attack)
  , release(apvts, "Release", parameters.release)
  , releaseDelay(apvts, "Release Delay", parameters.releaseDelay)
  , attackDelay(apvts, "Attack Delay", parameters.attackDelay)
  , metric(apvts, "Metric", { "Peak", "RMS" }, parameters.metric)
  , channelLabels(apvts, midSideParamID)
{
  addAndMakeVisible(attack);
  addAndMakeVisible(release);
  addAndMakeVisible(releaseDelay);
  addAndMakeVisible(attackDelay);
  addAndMakeVisible(metric);
  addAndMakeVisible(channelLabels);
  attack.tableSettings.drawLeftVericalLine = false;
  release.tableSettings.drawLeftVericalLine = false;
  releaseDelay.tableSettings.drawLeftVericalLine = false;
  attackDelay.tableSettings.drawLeftVericalLine = false;
  metric.tableSettings.drawLeftVericalLine = false;
  setSize(WIDTH, 120.f);
  setOpaque(false);
}

void
GammaEnvEditor::resized()
{
  int left = 0.f;

  auto const resize = [&](auto& component, int width) {
    component.setTopLeftPosition(left, 0.f);
    component.setSize(width, getHeight());
    left += width;
  };

  resize(channelLabels, 50);

  resize(metric, 90);
  resize(attack, 140);
  resize(release, 140);
  resize(attackDelay, 140);
  resize(releaseDelay, 140);
}

void
GammaEnvEditor::setTableSettings(LinkableControlTable tableSettings)
{
  channelLabels.tableSettings = tableSettings;
  tableSettings.drawLeftVericalLine = false;
  metric.tableSettings = tableSettings;
  attack.tableSettings = tableSettings;
  release.tableSettings = tableSettings;
  attackDelay.tableSettings = tableSettings;
  releaseDelay.tableSettings = tableSettings;
}
