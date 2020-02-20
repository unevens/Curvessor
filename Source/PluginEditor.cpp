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

  , inputGainLabels(*p.GetCurvessorParameters().apvts, "Mid-Side")

  , outputGainLabels(*p.GetCurvessorParameters().apvts, "Mid-Side")

  , background(ImageCache::getFromMemory(BinaryData::background_png,
                                         BinaryData::background_pngSize))
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
  addAndMakeVisible(inputGainLabels);
  addAndMakeVisible(outputGainLabels);
  addAndMakeVisible(midSideLabel);
  addAndMakeVisible(url);

  AttachSplineEditorsAndInitialize(splineEditor, nodeEditor);

  topologyLabel.setFont(Font(20, Font::bold));
  oversamplingLabel.setFont(Font(20, Font::bold));
  stereoLinkLabel.setFont(Font(20, Font::bold));
  midSideLabel.setFont(Font(20, Font::bold));

  topologyLabel.setJustificationType(Justification::centred);
  oversamplingLabel.setJustificationType(Justification::centred);
  stereoLinkLabel.setJustificationType(Justification::centred);
  midSideLabel.setJustificationType(Justification::centred);

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

  lineColour = p.looks.frontColour.darker(1.f);

  vuMeter.internalColour = backgroundColour;

  auto tableSettings = LinkableControlTable();
  tableSettings.lineColour = lineColour;
  tableSettings.backgroundColour = backgroundColour;
  gammaEnvEditor.setTableSettings(tableSettings);
  nodeEditor.setTableSettings(tableSettings);
  inputGain.tableSettings.drawLeftVericalLine = false;
  inputGain.tableSettings.lineColour = lineColour;
  inputGain.tableSettings.backgroundColour = backgroundColour;
  outputGain.tableSettings.drawLeftVericalLine = false;
  outputGain.tableSettings.lineColour = lineColour;
  outputGain.tableSettings.backgroundColour = backgroundColour;
  outputGainLabels.tableSettings.lineColour = lineColour;
  outputGainLabels.tableSettings.backgroundColour = backgroundColour;
  inputGainLabels.tableSettings.lineColour = lineColour;
  inputGainLabels.tableSettings.backgroundColour = backgroundColour;

  url.setFont({ 14, Font::bold });
  url.setJustification(Justification::centred);
  url.setReadOnly(true);
  url.setColour(TextEditor::ColourIds::focusedOutlineColourId, Colours::white);
  url.setColour(TextEditor::ColourIds::backgroundColourId,
                Colours::transparentBlack);
  url.setColour(TextEditor::ColourIds::outlineColourId,
                Colours::transparentBlack);
  url.setColour(TextEditor::ColourIds::textColourId,
                Colours::white.withAlpha(0.2f));
  url.setColour(TextEditor::ColourIds::highlightedTextColourId, Colours::white);
  url.setColour(TextEditor::ColourIds::highlightColourId, Colours::black);
  url.setText("www.unevens.net", dontSendNotification);
  url.setJustification(Justification::left);

  setSize(814, 890);

  startTimer(250);
}

CurvessorAudioProcessorEditor::~CurvessorAudioProcessorEditor() {}

void
CurvessorAudioProcessorEditor::paint(Graphics& g)
{
  g.drawImage(background, getLocalBounds().toFloat());

  g.setColour(backgroundColour);
  g.fillRect(635, 10, 160, 330);

  g.setColour(lineColour);
  g.drawRect(635, 10, 160, 330, 2);
  g.drawLine(636, 90, 795, 90, 2);
  g.drawLine(636, 155, 795, 155, 2);
  g.drawLine(636, 230, 795, 230, 2);
}

void
CurvessorAudioProcessorEditor::resized()
{
  constexpr int offset = 10;
  constexpr int rowHeight = 40;
  constexpr int splineEditorSide = 500;
  constexpr int vuMeterWidth = 90;
  constexpr int nodeEditorHeight = 160;

  splineEditor.setTopLeftPosition(offset, offset);
  splineEditor.setSize(splineEditorSide, splineEditorSide);

  vuMeter.setTopLeftPosition(splineEditorSide + 2 * offset, offset);
  vuMeter.setSize(vuMeterWidth, splineEditorSide);

  nodeEditor.setTopLeftPosition(offset, splineEditorSide + 2 * offset);
  nodeEditor.setSize(splineEditorSide + offset + vuMeterWidth, 160);

  int const gammaEnvEditorY = splineEditorSide + nodeEditorHeight + 3 * offset;
  gammaEnvEditor.setTopLeftPosition(
    offset, splineEditorSide + nodeEditorHeight + 3 * offset);
  gammaEnvEditor.setSize(GammaEnvEditor::WIDTH, rowHeight * 4);

  int const gainLeft = 3 * offset + splineEditorSide + vuMeterWidth;
  int const inputGainTop = 350;

  inputGainLabels.setTopLeftPosition(gainLeft, inputGainTop);
  inputGainLabels.setSize(50, 160);

  inputGain.setTopLeftPosition(gainLeft + 50, inputGainTop);
  inputGain.setSize(135, 160);

  int const outputGainTop = inputGainTop + 160 + offset;

  outputGainLabels.setTopLeftPosition(gainLeft, outputGainTop);
  outputGainLabels.setSize(50, 160);

  outputGain.setTopLeftPosition(gainLeft + 50, outputGainTop);
  outputGain.setSize(135, 160);

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateRows = { Track(40_px), Track(40_px), Track(40_px),
                        Track(30_px), Track(30_px), Track(40_px),
                        Track(40_px), Track(40_px), Track(30_px) };

  grid.templateColumns = { Track(1_fr) };

  grid.items = { GridItem(topologyLabel),
                 GridItem(topologyEditor.getControl())
                   .withWidth(100)
                   .withHeight(30)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(midSideLabel),
                 GridItem(midSideEditor.getControl())
                   .withWidth(30)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(stereoLinkLabel),
                 GridItem(stereoLink.getControl())
                   .withWidth(135)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(oversamplingLabel),
                 GridItem(oversampling)
                   .withWidth(70)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(linearPhase)
                   .withWidth(120)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center) };

  grid.justifyContent = Grid::JustifyContent::center;
  grid.alignContent = Grid::AlignContent::center;

  grid.performLayout(juce::Rectangle(
    splineEditorSide + vuMeterWidth + 3 * offset + 20, offset + 5, 150, 310));

  stereoLink.getControl().setTopLeftPosition(
    stereoLink.getControl().getPosition().x + 10,
    stereoLink.getControl().getPosition().y);

  url.setTopLeftPosition(10, getHeight() - 18);
  url.setSize(160, 16);

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
