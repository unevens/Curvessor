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

  , spline(*p.getCurvessorParameters().spline,
           *p.getCurvessorParameters().apvts)

  , selectedKnot(*p.getCurvessorParameters().spline,
                 *p.getCurvessorParameters().apvts)

  , gammaEnv(*p.getCurvessorParameters().apvts,
             p.getCurvessorParameters().envelopeFollower)

  , midSide(*this, *p.getCurvessorParameters().apvts, "Mid-Side")

  , sideChain(*this, *p.getCurvessorParameters().apvts, "Feedback-Amount")

  , vuMeter({ { &processor.gainVuMeterResults[0],
                &processor.gainVuMeterResults[1] } },
            36.f)

  , inputGain(*p.getCurvessorParameters().apvts,
              "Input Gain",
              p.getCurvessorParameters().inputGain)

  , outputGain(*p.getCurvessorParameters().apvts,
               "Output Gain",
               p.getCurvessorParameters().outputGain)

  , wet(*p.getCurvessorParameters().apvts,
        "Wet",
        p.getCurvessorParameters().wet)

  , feedbackAmount(*p.getCurvessorParameters().apvts,
                   "Feedback",
                   p.getCurvessorParameters().feedbackAmount)

  , highPassCutoff(*p.getCurvessorParameters().apvts,
                   "Cutoff",
                   p.getCurvessorParameters().highPassCutoff)

  , highPassOrder(*this,
                  *p.getCurvessorParameters().apvts,
                  "High-Pass-Order",
                  { "Disabled", "6dB/Oct", "12db/Oct", "18dB/Oct" })

  , stereoLink(*this, *p.getCurvessorParameters().apvts, "Stereo-Link")

  , ioGainLabels(*p.getCurvessorParameters().apvts, "Mid-Side")

  , highPassCutoffLabels(*p.getCurvessorParameters().apvts, "Mid-Side")

  , oversampling(*this,
                 *p.getCurvessorParameters().apvts,
                 "Oversampling",
                 { "1x", "2x", "4x", "8x", "16x", "32x" })

  , linearPhase(*this,
                *p.getCurvessorParameters().apvts,
                "Linear-Phase-Oversampling")

  , smoothing(*this, *p.getCurvessorParameters().apvts, "Smoothing-Time")

  , background(ImageCache::getFromMemory(BinaryData::background_png,
                                         BinaryData::background_pngSize))
{
  addAndMakeVisible(spline);
  addAndMakeVisible(selectedKnot);
  addAndMakeVisible(inputGain);
  addAndMakeVisible(outputGain);
  addAndMakeVisible(wet);
  addAndMakeVisible(feedbackAmount);
  addAndMakeVisible(gammaEnv);
  addAndMakeVisible(vuMeter);
  addAndMakeVisible(stereoLinkLabel);
  addAndMakeVisible(oversamplingLabel);
  addAndMakeVisible(ioGainLabels);
  addAndMakeVisible(highPassCutoffLabels);
  addAndMakeVisible(smoothingLabel);
  addAndMakeVisible(highPassCutoff);
  addAndMakeVisible(highPassLabelFirsLine);
  addAndMakeVisible(highPassLabelSecondLine);
  addAndMakeVisible(url);

  spline.xSuffix = "dB";
  spline.ySuffix = "dB";

  attachAndInitializeSplineEditors(spline, selectedKnot, 3);

  oversamplingLabel.setFont(Font(20._p, Font::bold));
  stereoLinkLabel.setFont(Font(20._p, Font::bold));
  midSide.getControl().setButtonText("Mid Side");
  sideChain.getControl().setButtonText("SideChain");

  oversamplingLabel.setJustificationType(Justification::centred);
  stereoLinkLabel.setJustificationType(Justification::centred);
  smoothingLabel.setJustificationType(Justification::centred);
  highPassLabelFirsLine.setJustificationType(Justification::centred);
  highPassLabelSecondLine.setJustificationType(Justification::centred);

  smoothing.getControl().setTextValueSuffix("ms");

  for (int c = 0; c < 2; ++c) {
    spline.vuMeter[c] = &processor.levelVuMeterResults[c];
    feedbackAmount.getControl(c).setTextValueSuffix("%");
    highPassCutoff.getControl(c).setTextValueSuffix("hz");
  }

  linearPhase.getControl().setButtonText("Linear Phase");

  stereoLink.getControl().setTextValueSuffix("%");

  lineColour = p.looks.frontColour.darker(1.f);

  vuMeter.internalColour = backgroundColour;

  auto tableSettings = LinkableControlTable();
  tableSettings.lineColour = lineColour;
  tableSettings.backgroundColour = backgroundColour;
  gammaEnv.setTableSettings(tableSettings);
  selectedKnot.setTableSettings(tableSettings);

  auto const applyTableSettings = [&](auto& linkedControls) {
    linkedControls.tableSettings.lineColour = lineColour;
    linkedControls.tableSettings.backgroundColour = backgroundColour;
  };

  applyTableSettings(inputGain);
  applyTableSettings(outputGain);
  applyTableSettings(ioGainLabels);
  applyTableSettings(highPassCutoffLabels);
  applyTableSettings(feedbackAmount);
  applyTableSettings(highPassCutoff);
  applyTableSettings(wet);

  for (int c = 0; c < 2; ++c) {
    outputGain.getControl(c).setTextValueSuffix("dB");
    inputGain.getControl(c).setTextValueSuffix("dB");
    wet.getControl(c).setTextValueSuffix("%");
    wet.getControl(c).setTextValueSuffix("%");
  }

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

  setSize(1022._p, 966._p);
}

CurvessorAudioProcessorEditor::~CurvessorAudioProcessorEditor() {}

constexpr auto offset = 10._p;
constexpr auto rowHeight = 40._p;
constexpr auto splineEditorSide = 572._p;
constexpr auto vuMeterWidth = 89._p;
constexpr auto knotEditorHeight = 160._p;
constexpr auto gainLeft = 3 * offset + splineEditorSide + vuMeterWidth;
constexpr auto ioGainTop = splineEditorSide + 2 * offset;
constexpr auto highPassTop = splineEditorSide + offset - rowHeight * 4;
constexpr auto offsetFromRight = 241._p;
constexpr auto channelLabelsWidth = 55._p;

void
CurvessorAudioProcessorEditor::paint(Graphics& g)
{
  g.drawImage(background, getLocalBounds().toFloat());

  auto const left = getRight() - offsetFromRight - 5._p;

  g.setColour(backgroundColour);
  g.fillRect(juce::Rectangle<float>(left, 10._p, 160._p, 400._p));
  g.fillRect(juce::Rectangle<float>(gainLeft, highPassTop, 136._p, 160._p));

  g.setColour(lineColour);

  g.drawRect(left, 10._p, 160._p, 45._p, 1);
  g.drawRect(left, 10._p, 160._p, 90._p, 1);
  g.drawRect(left, 10._p, 160._p, 180._p, 1);
  g.drawRect(left, 10._p, 160._p, 270._p, 1);
  g.drawRect(left, 10._p, 160._p, 400._p, 1);

  g.drawRect(spline.getBounds().expanded(1, 1), 1);

  g.drawRect(gainLeft, highPassTop, 136._p, 160._p, 1);
  g.drawRect(gainLeft + 10._p, highPassTop + 10._p, 116._p, 60._p, 1);
  g.drawLine(gainLeft + 30._p,
             highPassTop + 80._p,
             gainLeft + 106._p,
             highPassTop + 80._p,
             1);
}

void
CurvessorAudioProcessorEditor::resized()
{
  spline.setTopLeftPosition(offset + 1, offset + 1);
  spline.setSize(splineEditorSide - 2, splineEditorSide - 2);

  vuMeter.setTopLeftPosition(splineEditorSide + 2 * offset, offset);
  vuMeter.setSize(vuMeterWidth, splineEditorSide);

  selectedKnot.setTopLeftPosition(offset, splineEditorSide + 2 * offset);
  selectedKnot.setSize(splineEditorSide + offset + vuMeterWidth + 2, 160._p);

  constexpr auto gammaEnvEditorY =
    splineEditorSide + knotEditorHeight + 3 * offset;

  gammaEnv.setTopLeftPosition(offset, gammaEnvEditorY);
  gammaEnv.setSize(gammaEnv.fullSizeWidth * uiGlobalScaleFactor, rowHeight * 4);

  highPassLabelFirsLine.setTopLeftPosition(gainLeft, highPassTop + 10._p);
  highPassLabelFirsLine.setSize(136._p, rowHeight);

  highPassLabelSecondLine.setTopLeftPosition(gainLeft, highPassTop + 30._p);
  highPassLabelSecondLine.setSize(136._p, rowHeight);

  highPassOrder.getControl().setTopLeftPosition(gainLeft + 10._p,
                                                highPassTop + 2.5 * rowHeight);
  highPassOrder.getControl().setSize(116._p, rowHeight);

  highPassCutoffLabels.setTopLeftPosition(gainLeft + 136._p - 2, highPassTop);
  highPassCutoffLabels.setSize(channelLabelsWidth, rowHeight * 4);

  highPassCutoff.setTopLeftPosition(highPassCutoffLabels.getRight() - 2,
                                    highPassTop);
  highPassCutoff.setSize(136._p, rowHeight * 4);

  ioGainLabels.setTopLeftPosition(gainLeft, ioGainTop);
  ioGainLabels.setSize(channelLabelsWidth, rowHeight * 4);

  inputGain.setTopLeftPosition(gainLeft + channelLabelsWidth - 1, ioGainTop);
  inputGain.setSize(136._p, rowHeight * 4);

  outputGain.setTopLeftPosition(inputGain.getRight() - 2, ioGainTop);
  outputGain.setSize(136._p, rowHeight * 4);

  feedbackAmount.setTopLeftPosition(inputGain.getPosition().getX(),
                                    gammaEnvEditorY);
  feedbackAmount.setSize(136._p, rowHeight * 4);

  wet.setTopLeftPosition(feedbackAmount.getRight() - 2, gammaEnvEditorY);
  wet.setSize(136._p + 1, rowHeight * 4);

  Grid grid;
  using Track = Grid::TrackInfo;

  grid.templateRows = { Track(Grid::Px(44._p)), Track(Grid::Px(44._p)),
                        Track(Grid::Px(44._p)), Track(Grid::Px(44._p)),
                        Track(Grid::Px(44._p)), Track(Grid::Px(44._p)),
                        Track(Grid::Px(48._p)), Track(Grid::Px(40._p)),
                        Track(Grid::Px(40._p)) };

  grid.templateColumns = { Track(1_fr) };

  grid.items = {
    GridItem(sideChain.getControl())
      .withWidth(120._p)
      .withAlignSelf(GridItem::AlignSelf::center)
      .withJustifySelf(GridItem::JustifySelf::center),
    GridItem(midSide.getControl())
      .withWidth(120._p)
      .withAlignSelf(GridItem::AlignSelf::center)
      .withJustifySelf(GridItem::JustifySelf::center),
    GridItem(stereoLinkLabel)
      .withAlignSelf(GridItem::AlignSelf::start)
      .withJustifySelf(GridItem::JustifySelf::center),
    GridItem(stereoLink.getControl())
      .withWidth(135._p)
      .withAlignSelf(GridItem::AlignSelf::center)
      .withJustifySelf(GridItem::JustifySelf::center),
    GridItem(smoothingLabel)
      .withAlignSelf(GridItem::AlignSelf::start)
      .withJustifySelf(GridItem::JustifySelf::center),
    GridItem(smoothing.getControl())
      .withWidth(135._p)
      .withAlignSelf(GridItem::AlignSelf::start)
      .withJustifySelf(GridItem::JustifySelf::center),
    GridItem(oversamplingLabel).withAlignSelf(GridItem::AlignSelf::start),
    GridItem(oversampling.getControl())
      .withWidth(std::max(60.0L, 70._p))
      .withHeight(30._p)
      .withAlignSelf(GridItem::AlignSelf::start)
      .withJustifySelf(GridItem::JustifySelf::center),
    GridItem(linearPhase.getControl())
      .withWidth(130._p)
      .withAlignSelf(GridItem::AlignSelf::start)
      .withJustifySelf(GridItem::JustifySelf::center)
  };

  grid.justifyContent = Grid::JustifyContent::center;
  grid.alignContent = Grid::AlignContent::center;

  grid.performLayout(juce::Rectangle<int>(
    getRight() - offsetFromRight, offset - 2._p, 150._p, 400._p));

  url.setTopLeftPosition(10._p, getHeight() - 18._p);
  url.setSize(160._p, 16._p);

  spline.areaInWhichToDrawKnots =
    juce::Rectangle<int>(spline.getPosition().x,
                         spline.getPosition().y,
                         jmax(spline.getWidth(), selectedKnot.getWidth()),
                         selectedKnot.getBottom() - spline.getPosition().y);
}
