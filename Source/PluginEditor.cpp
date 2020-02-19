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

  , gammaEnvEditor(*p.GetCurvessorParameters().apvts,
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

  , inputGain(*p.GetCurvessorParameters().apvts,
              "Input Gain",
              p.GetCurvessorParameters().inputGain)

  , outputGain(*p.GetCurvessorParameters().apvts,
               "Output Gain",
               p.GetCurvessorParameters().outputGain)

  , stereoLink(*this, *p.GetCurvessorParameters().apvts, "Stereo-Link")
{

  addAndMakeVisible(splineEditor);
  addAndMakeVisible(nodeEditor);
  addAndMakeVisible(inputGain);
  addAndMakeVisible(outputGain);
  addAndMakeVisible(gammaEnvEditor);
  addAndMakeVisible(vuMeter);
  addAndMakeVisible(topologyLabel);
  addAndMakeVisible(stereoLinkLabel);
  addAndMakeVisible(oversamplingLabel);
  addAndMakeVisible(oversampling);
  addAndMakeVisible(linearPhase);

  AttachSplineEditorsAndInitialize(splineEditor, nodeEditor);

  inputGain.tableSettings.drawLeftVericalLine = false;
  outputGain.tableSettings.drawLeftVericalLine = false;

  midSideEditor.getControl().setButtonText("Mid-Side");

  for (int c = 0; c < 2; ++c) {
    splineEditor.vuMeter[c] = &processor.levelVuMeterResults[c];
  }

  linearPhase.setButtonText("Linear Phase");

  for (int i = 0; i <= 5; ++i) {
    oversampling.addItem(std::to_string(1 << i) + "x", i + 1);
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
  constexpr int offset = 10;
  constexpr int rowHeight = 30;
  constexpr int splineEditorSide = 500;
  constexpr int vuMeterWidth = 90;
  constexpr int nodeEditorHeight = 120;

  splineEditor.setTopLeftPosition(offset, offset);
  splineEditor.setSize(splineEditorSide, splineEditorSide);

  vuMeter.setTopLeftPosition(splineEditorSide + 2 * offset, offset);
  vuMeter.setSize(vuMeterWidth, splineEditorSide);

  nodeEditor.setTopLeftPosition(offset, splineEditorSide + 2 * offset);
  nodeEditor.setSize(splineEditorSide + offset + vuMeterWidth, 200);

  int const gammaEnvEditorY = splineEditorSide + nodeEditorHeight + 3 * offset;
  gammaEnvEditor.setTopLeftPosition(
    offset, splineEditorSide + nodeEditorHeight + 3 * offset);
  gammaEnvEditor.setSize(GammaEnvEditor::WIDTH, rowHeight * 4);

  inputGain.setTopLeftPosition(offset + GammaEnvEditor::WIDTH, gammaEnvEditorY);
  inputGain.setSize(100, rowHeight * 4);
  outputGain.setTopLeftPosition(offset + GammaEnvEditor::WIDTH + 100,
                                gammaEnvEditorY);
  outputGain.setSize(100, rowHeight * 4);

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateRows = { Track(1_fr), Track(1_fr), Track(1_fr), Track(1_fr),
                        Track(1_fr), Track(1_fr), Track(1_fr), Track(1_fr) };

  grid.templateColumns = { Track(1_fr) };

  grid.items = { GridItem(topologyLabel),
                 GridItem(topologyEditor.getControl()),
                 GridItem(midSideEditor.getControl()),
                 GridItem(stereoLinkLabel),
                 GridItem(stereoLink.getControl()),
                 GridItem(oversamplingLabel),
                 GridItem(oversampling),
                 GridItem(linearPhase) };

  grid.performLayout(juce::Rectangle<int>(
    splineEditorSide + vuMeterWidth + 3 * offset, 0, 100.f, 280.f));

  splineEditor.areaInWhichToDrawNodes = juce::Rectangle(
    splineEditor.getPosition().x,
    splineEditor.getPosition().x,
    jmax(splineEditor.getWidth(), nodeEditor.getWidth()),
    nodeEditor.getPosition().y + nodeEditor.getHeight() + offset);
}

void
CurvessorAudioProcessorEditor::timerCallback()
{
  processor.oversamplingGuiGetter.update();
  auto& overSettings = processor.oversamplingGuiGetter.get();

  linearPhase.setToggleState(overSettings.linearPhase, dontSendNotification);
  oversampling.setSelectedId(overSettings.order + 1, dontSendNotification);
}
