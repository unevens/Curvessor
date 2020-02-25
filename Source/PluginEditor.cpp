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

  , splineEditor(*p.getCurvessorParameters().spline,
                 *p.getCurvessorParameters().apvts)

  , nodeEditor(*p.getCurvessorParameters().spline,
               *p.getCurvessorParameters().apvts)

  , gammaEnvEditor(*p.getCurvessorParameters().apvts,
                   p.getCurvessorParameters().envelopeFollower)

  , midSideEditor(*this, *p.getCurvessorParameters().apvts, "Mid-Side")

  , topologyEditor(*this,
                   *p.getCurvessorParameters().apvts,
                   "Topology",
                   { "Forward", "Feedback", "SideChain" })

  , vuMeter({ { &processor.gainVuMeterResults[0],
                &processor.gainVuMeterResults[1] } },
            36.f,
            [](float x) { return std::pow(x, 1.f / 2.f); })

  , inputGain(*p.getCurvessorParameters().apvts,
              "Input Gain",
              p.getCurvessorParameters().inputGain)

  , outputGain(*p.getCurvessorParameters().apvts,
               "Output Gain",
               p.getCurvessorParameters().outputGain)

  , stereoLink(*this, *p.getCurvessorParameters().apvts, "Stereo-Link")

  , inputGainLabels(*p.getCurvessorParameters().apvts, "Mid-Side")

  , outputGainLabels(*p.getCurvessorParameters().apvts, "Mid-Side")

  , oversampling(*this,
                 *p.getCurvessorParameters().apvts,
                 "Oversampling",
                 { "1x", "2x", "4x", "8x", "16x", "32x" })

  , linearPhase(*this,
                *p.getCurvessorParameters().apvts,
                "Linear-Phase-Oversampling")

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
  addAndMakeVisible(inputGainLabels);
  addAndMakeVisible(outputGainLabels);
  addAndMakeVisible(midSideLabel);
  addAndMakeVisible(url);

  AttachSplineEditorsAndInitialize(splineEditor, nodeEditor);

  topologyLabel.setFont(Font(20._p, Font::bold));
  oversamplingLabel.setFont(Font(20._p, Font::bold));
  stereoLinkLabel.setFont(Font(20._p, Font::bold));
  midSideLabel.setFont(Font(20._p, Font::bold));

  topologyLabel.setJustificationType(Justification::centred);
  oversamplingLabel.setJustificationType(Justification::centred);
  stereoLinkLabel.setJustificationType(Justification::centred);
  midSideLabel.setJustificationType(Justification::centred);

  for (int c = 0; c < 2; ++c) {
    splineEditor.vuMeter[c] = &processor.levelVuMeterResults[c];
  }

  linearPhase.getControl().setButtonText("Linear Phase");

  lineColour = p.looks.frontColour.darker(1.f);

  vuMeter.internalColour = backgroundColour;

  auto tableSettings = LinkableControlTable();
  tableSettings.lineColour = lineColour;
  tableSettings.backgroundColour = backgroundColour;
  gammaEnvEditor.setTableSettings(tableSettings);
  nodeEditor.setTableSettings(tableSettings);
  inputGain.tableSettings.lineColour = lineColour;
  inputGain.tableSettings.backgroundColour = backgroundColour;
  outputGain.tableSettings.lineColour = lineColour;
  outputGain.tableSettings.backgroundColour = backgroundColour;
  outputGainLabels.tableSettings.lineColour = lineColour;
  outputGainLabels.tableSettings.backgroundColour = backgroundColour;
  inputGainLabels.tableSettings.lineColour = lineColour;
  inputGainLabels.tableSettings.backgroundColour = backgroundColour;

  url.setFont({ 14._p, Font::bold });
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

  setSize(814._p, 890._p);
}

CurvessorAudioProcessorEditor::~CurvessorAudioProcessorEditor() {}

void
CurvessorAudioProcessorEditor::paint(Graphics& g)
{
  g.drawImage(background, getLocalBounds().toFloat());

  g.setColour(backgroundColour);
  g.fillRect(juce::Rectangle<float>(632._p, 10._p, 160._p, 330._p));

  g.setColour(lineColour);
  g.drawRect(632._p, 10._p, 160._p, 80._p, 1);
  g.drawRect(632._p, 10._p, 160._p, 145._p, 1);
  g.drawRect(632._p, 10._p, 160._p, 220._p, 1);
  g.drawRect(632._p, 10._p, 160._p, 330._p, 1);

  g.drawRect(splineEditor.getBounds().expanded(1, 1), 1);
}
void
CurvessorAudioProcessorEditor::resized()
{
  constexpr auto offset = 10._p;
  constexpr auto rowHeight = 40._p;
  constexpr auto splineEditorSide = 500._p;
  constexpr auto vuMeterWidth = 89._p;
  constexpr auto nodeEditorHeight = 160._p;

  splineEditor.setTopLeftPosition(offset + 1, offset + 1);
  splineEditor.setSize(splineEditorSide - 2, splineEditorSide - 2);

  vuMeter.setTopLeftPosition(splineEditorSide + 2 * offset, offset);
  vuMeter.setSize(vuMeterWidth, splineEditorSide);

  nodeEditor.setTopLeftPosition(offset, splineEditorSide + 2 * offset);
  nodeEditor.setSize(splineEditorSide + offset + vuMeterWidth + 2, 160._p);

  constexpr auto gammaEnvEditorY =
    splineEditorSide + nodeEditorHeight + 3 * offset;

  gammaEnvEditor.setTopLeftPosition(offset, gammaEnvEditorY);
  gammaEnvEditor.setSize(gammaEnvEditor.fullSizeWidth * uiGlobalScaleFactor,
                         rowHeight * 4);

  constexpr auto gainLeft = 3 * offset + splineEditorSide + vuMeterWidth;
  constexpr auto inputGainTop = 350._p;

  inputGainLabels.setTopLeftPosition(gainLeft, inputGainTop);
  inputGainLabels.setSize(50._p, 160._p);

  inputGain.setTopLeftPosition(gainLeft + 50._p - 1, inputGainTop);
  inputGain.setSize(136._p, 160._p);

  constexpr auto outputGainTop = inputGainTop + 160._p + offset;

  outputGainLabels.setTopLeftPosition(gainLeft, outputGainTop);
  outputGainLabels.setSize(50._p, 160._p);

  outputGain.setTopLeftPosition(gainLeft + 50._p - 1, outputGainTop);
  outputGain.setSize(136._p, 160._p);

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateRows = { Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(30._p)),
                        Track(Grid::Px(30._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(30._p)) };

  grid.templateColumns = { Track(1_fr) };

  grid.items = { GridItem(topologyLabel),
                 GridItem(topologyEditor.getControl())
                   .withWidth(120._p)
                   .withHeight(30._p)
                   .withAlignSelf(GridItem::AlignSelf::start)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(midSideLabel),
                 GridItem(midSideEditor.getControl())
                   .withWidth(30._p)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(stereoLinkLabel),
                 GridItem(stereoLink.getControl())
                   .withWidth(135._p)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(oversamplingLabel),
                 GridItem(oversampling.getControl())
                   .withWidth(std::max(60.0L, 70._p))
                   .withHeight(30._p)
                   .withAlignSelf(GridItem::AlignSelf::start)
                   .withJustifySelf(GridItem::JustifySelf::center),
                 GridItem(linearPhase.getControl())
                   .withWidth(130._p)
                   .withAlignSelf(GridItem::AlignSelf::center)
                   .withJustifySelf(GridItem::JustifySelf::center) };

  grid.justifyContent = Grid::JustifyContent::center;
  grid.alignContent = Grid::AlignContent::center;

  grid.performLayout(
    juce::Rectangle<int>(splineEditorSide + vuMeterWidth + 3 * offset + 17._p,
                         offset + 5._p,
                         150._p,
                         310._p));

  midSideEditor.getControl().setTopLeftPosition(
    midSideEditor.getControl().getPosition().x,
    midSideEditor.getControl().getPosition().y - 6._p);

  stereoLink.getControl().setTopLeftPosition(
    stereoLink.getControl().getPosition().x + 10._p,
    stereoLink.getControl().getPosition().y);

  url.setTopLeftPosition(10._p, getHeight() - 18._p);
  url.setSize(160._p, 16._p);

  splineEditor.areaInWhichToDrawNodes = juce::Rectangle<int>(
    splineEditor.getPosition().x,
    splineEditor.getPosition().x,
    jmax(splineEditor.getWidth(), nodeEditor.getWidth()),
    nodeEditor.getPosition().y + nodeEditor.getHeight() + offset);
}
