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

#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
CurvessorAudioProcessorEditor::CurvessorAudioProcessorEditor(
  CurvessorAudioProcessor& p)

  : AudioProcessorEditor(&p)

  , processor(p)

  , splineEditor(*p.GetCurvessorParameters().spline,
                 *p.GetCurvessorParameters().apvts)

  , nodeEditor(*p.GetCurvessorParameters().spline,
               *p.GetCurvessorParameters().apvts)

  , gammaEnvEditor(*this,
                   *p.GetCurvessorParameters().apvts,
                   p.GetCurvessorParameters().envelopeFollower)

  , midSideEditor(*this, *p.GetCurvessorParameters().apvts, "Mid-Side")

  , topologyEditor(*this,
                   *p.GetCurvessorParameters().apvts,
                   "Topology",
                   { "Forward", "Feedback", "SideChain" })

  , vuMeter({ { &processor.gainVuMeterResults[0],
                &processor.gainVuMeterResults[1] } },
            36.f,
            [](float x) { return std::pow(x, 1.f / 2.f); })

  , inputGain(*this,
              *p.GetCurvessorParameters().apvts,
              "Input Gain",
              p.GetCurvessorParameters().inputGain)

  , outputGain(*this,
               *p.GetCurvessorParameters().apvts,
               "Output Gain",
               p.GetCurvessorParameters().outputGain)
{
  addAndMakeVisible(splineEditor);
  addAndMakeVisible(nodeEditor);

  AttachSplineEditorsAndInitialize(splineEditor, nodeEditor);

  addAndMakeVisible(vuMeter);

  addAndMakeVisible(topologyLabel);

  midSideEditor.getControl().setButtonText("Mid-Side");

  for (int c = 0; c < 2; ++c) {
    splineEditor.vuMeter[c] = &processor.levelVuMeterResults[c];
  }

  addAndMakeVisible(oversampling);
  addAndMakeVisible(linearPhase);

  linearPhase.setButtonText("Linear Phase");
  for (int i = 0; i <= 5; ++i) {
    oversampling.addItem(std::to_string(1 << i) + "x Oversampling", i + 1);
  }

  auto const OnOversamplingChange = [this] {
    bool isLinearPhase = linearPhase.getToggleState();
    int order = oversampling.getSelectedId() - 1;
    processor.asyncOversampling.submitMessage(
      [=](oversimple::OversamplingSettings& oversampling) {
        oversampling.linearPhase = isLinearPhase;
        oversampling.order = order;
      });
  };

  linearPhase.onClick = OnOversamplingChange;
  oversampling.onChange = OnOversamplingChange;

  setSize(1000, 900);

  startTimer(250);
}

CurvessorAudioProcessorEditor::~CurvessorAudioProcessorEditor() {}

//==============================================================================
void
CurvessorAudioProcessorEditor::paint(Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void
CurvessorAudioProcessorEditor::resized()
{
  splineEditor.setTopLeftPosition(0, 0);
  splineEditor.setSize(400, 400);

  nodeEditor.setTopLeftPosition(0, 400);
  nodeEditor.setSize(600, 200);

  float gammaEnvEditorRight = getWidth() - 200;
  float rowHeight = 30.f;
  gammaEnvEditor.resizeLinkables(
    { 0, 600 }, getWidth() - 200, rowHeight, 2.f, 2.f);

  inputGain.resize({ gammaEnvEditorRight, 600.f }, 100, rowHeight, 2.f);
  outputGain.resize(
    { gammaEnvEditorRight + 100.f, 600.f }, 100, rowHeight, 2.f);

  vuMeter.setTopLeftPosition(410, 0);
  vuMeter.setSize(90, 400);

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateRows = { Track(1_fr), Track(1_fr) };
  grid.templateColumns = { Track(1_fr), Track(1_fr) };

  grid.items = { GridItem(topologyLabel),
                 GridItem(gammaEnvEditor.stereoLinkLabel),
                 GridItem(topologyEditor.getControl()),
                 GridItem(gammaEnvEditor.stereoLink.getControl()) };

  grid.performLayout(juce::Rectangle<int>(0, 750, 300.f, 100.f));

  midSideEditor.getControl().setTopLeftPosition(500, 750);
  midSideEditor.getControl().setSize(200, 40);

  oversampling.setTopLeftPosition(600, 750);
  oversampling.setSize(200, 40);
  linearPhase.setTopLeftPosition(600, 850);
  linearPhase.setSize(200, 40);

  splineEditor.areaInWhichToDrawNodes =
    juce::Rectangle(splineEditor.getPosition().x,
                    splineEditor.getPosition().x,
                    jmax(splineEditor.getWidth(), nodeEditor.getWidth()),
                    nodeEditor.getPosition().y + nodeEditor.getHeight());
}

void
CurvessorAudioProcessorEditor::timerCallback()
{
  processor.oversamplingGuiGetter.update();
  auto& overSettings = processor.oversamplingGuiGetter.get();

  linearPhase.setToggleState(overSettings.linearPhase, dontSendNotification);
  oversampling.setSelectedId(overSettings.order + 1, dontSendNotification);
}
