#pragma once

#include "IPlug_include_in_plug_hdr.h"

// JUCE-free Curvessor DSP + its dependencies (audio-dsp, oversimple, avec).
#include "CurvessorDsp.h"
#include "oversimple/Oversampling.hpp"
#include "avec/Alignment.hpp"
#include "avec/Buffer.hpp"

// ISender for audio-thread → UI-thread VU meter packets.
#include "ISender.h"

// Forward declarations to keep IControls.h out of the plugin header — the
// .cpp includes it directly when it needs the full ITextControl /
// IVTrackControlBase definitions.
namespace iplug {
namespace igraphics {
  class ITextControl;
  class IVTrackControlBase;
}
}

#include <array>
#include <atomic>
#include <mutex>

const int kNumPresets = 1;

// Parameter layout — mirrors the JUCE-side APVTS layout. See:
//   /Users/io/dev/Curvessor/Source/PluginProcessor.cpp ::Parameters
//   /Users/io/dev/Curvessor/juicy/SplineParameters.cpp ::SplineParameters
// Linkable pairs are stored as (ch0, ch1, link-bool) triples; knots as
// 10-param blocks matching SplineParameters' construction order.

#define CURVESSOR_KNOT_ENUM(i)                                                 \
  kKnot##i##_enabled, kKnot##i##_linked,                                       \
  kKnot##i##_X_ch0,   kKnot##i##_Y_ch0,                                        \
  kKnot##i##_Tan_ch0, kKnot##i##_Smooth_ch0,                                   \
  kKnot##i##_X_ch1,   kKnot##i##_Y_ch1,                                        \
  kKnot##i##_Tan_ch1, kKnot##i##_Smooth_ch1

enum EParams
{
  // -- Globals --
  kMidSide = 0,
  kSideChain,
  kOversampling,
  kLinearPhaseOversampling,
  kStereoLink,
  kSmoothingTime,
  kHighPassOrder,

  // -- Linkable float pairs (ch0, ch1, link-bool) --
  kInputGain_ch0,      kInputGain_ch1,      kInputGain_link,
  kOutputGain_ch0,     kOutputGain_ch1,     kOutputGain_link,
  kWet_ch0,            kWet_ch1,            kWet_link,
  kFeedbackAmount_ch0, kFeedbackAmount_ch1, kFeedbackAmount_link,
  kAttack_ch0,         kAttack_ch1,         kAttack_link,
  kRelease_ch0,        kRelease_ch1,        kRelease_link,
  kAttackDelay_ch0,    kAttackDelay_ch1,    kAttackDelay_link,
  kReleaseDelay_ch0,   kReleaseDelay_ch1,   kReleaseDelay_link,
  kRMSTime_ch0,        kRMSTime_ch1,        kRMSTime_link,
  kHighPassCutoff_ch0, kHighPassCutoff_ch1, kHighPassCutoff_link,

  // -- Spline knots (8 editable × 10 = 80). Slot 0 is a fixed knot at
  //    (-96, -96, t=1, s=1) — not parameter-backed, set up in OnReset.
  CURVESSOR_KNOT_ENUM(1),
  CURVESSOR_KNOT_ENUM(2),
  CURVESSOR_KNOT_ENUM(3),
  CURVESSOR_KNOT_ENUM(4),
  CURVESSOR_KNOT_ENUM(5),
  CURVESSOR_KNOT_ENUM(6),
  CURVESSOR_KNOT_ENUM(7),
  CURVESSOR_KNOT_ENUM(8),

  kNumParams
};

#undef CURVESSOR_KNOT_ENUM

// Layout invariants used by the loop-based Init code and (later) the DSP.
static_assert(kInputGain_ch1   - kInputGain_ch0   == 1, "linkable layout");
static_assert(kInputGain_link  - kInputGain_ch0   == 2, "linkable layout");
static_assert(kOutputGain_ch0  - kInputGain_ch0   == 3, "linkable triple stride");
static_assert(kKnot2_enabled   - kKnot1_enabled   == 10, "knot stride");

enum ECtrlTags
{
  kCtrlTagTitle = 0,
  kCtrlTagVersionNumber,
  kCtrlTagInputMeter,
  kCtrlTagGainMeter,
  kCtrlTagOutputMeter,
  kCtrlTagSplineEditor,
};

using namespace iplug;
using namespace igraphics;

class Curvessor final : public Plugin
{
public:
  Curvessor(const InstanceInfo& info);

  // Runs on the UI thread. Drains both sender queues and pushes packets to
  // the meter controls via their ctrl tags.
  void OnIdle() override;

  // Fires when the host closes the plugin editor. IGraphics destroys all
  // controls on close (RemoveAllControls in OnViewDestroyed), so we must
  // null our cached side-panel control pointers before the idle timer
  // dereferences them — that path crashed Reaper on close.
  void OnUIClose() override;

  // Linkable-pair propagation: when a ch0 or ch1 param changes and the
  // pair's _is_linked toggle is on, copy the change to the other channel
  // so both knobs stay in sync during a drag (and both stay in sync at
  // rest, so unlinking later doesn't pop ch1 to a stale value).
  void OnParamChangeUI(int paramIdx, EParamSource source = kUnknown) override;

private:
  // Guards against infinite recursion when OnParamChangeUI calls
  // SendParameterValueFromUI on the other channel — that call re-enters
  // OnParamChangeUI for the other param, which would otherwise try to
  // mirror back to the first and loop forever.
  bool mPropagatingLinkChange = false;

public:

#if IPLUG_EDITOR
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override { return true; }
#endif

#if IPLUG_DSP
  void OnReset() override;
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;

private:
  // Read all 80 spline-knot params + the single fixed anchor into mDsp->autoSpline.
  // Returns the number of active knots (fixed anchor + each enabled editable knot).
  // Mirrors juicy/SplineParameters::updateSpline(AutoSpline&).
  int UpdateSplineFromParams();

  // Reconfigure the 3 oversamplers if Oversampling or Linear-Phase-Oversampling
  // changed since the last call. Called from ProcessBlock under mOversamplingMutex.
  void EnsureOversamplingCurrent();

  // Read a linkable float-pair param honoring its _is_linked toggle. When
  // linked, both channels return the value at ch0Idx; otherwise each channel
  // returns its own. Matches juicy/LinkableParameter::get(channel).
  double GetLinkable(int ch0Idx, int channel) const
  {
    const bool linked = GetParam(ch0Idx + 2)->Bool();
    return GetParam(ch0Idx + (linked ? 0 : channel))->Value();
  }
#endif

#if IPLUG_DSP
  avec::aligned_ptr<curvessor::Dsp> mDsp;

  oversimple::OversamplingSettings mOversamplingSettings;
  oversimple::TOversampling<double> mWetOversampling;
  oversimple::TOversampling<double> mDryOversampling;
  oversimple::TOversampling<double> mSidechainOversampling;
  std::recursive_mutex mOversamplingMutex;

  // Tracks the last applied oversampling settings so we can detect param drift.
  int mLastOversamplingOrder = 0;
  bool mLastOversamplingLinearPhase = false;

  adsp::GammaEnvSettings<Vec2d> mEnvelopeFollowerSettings;

  // Holds the dry signal for wet/dry blending after oversampling round-trip.
  avec::Buffer<double> mDryBuffer{2};

  // Scratch for sidechain input copy + in-place M/S / input-gain ramp before
  // oversampling. Allocated to GetBlockSize() in OnReset.
  avec::Buffer<double> mSidechainScratch{2};
#endif

#if IPLUG_DSP || IPLUG_EDITOR
  // VU meter snapshots: written from the audio thread, read from the GUI timer.
  std::array<std::atomic<float>, 2> mLevelVuMeterResults{};
  std::array<std::atomic<float>, 2> mGainVuMeterResults{};

  // Cross-thread queues for the IVMeterControl widgets. Carrying linear
  // amplitudes (not dB) — IVMeterControl::EResponse::Log calls AmpToDB
  // internally and maps the result to [0,1] for the bar fill.
  //
  // We send three meter streams and one curve-overlay stream:
  //
  //   mInputLevelSender — post-input-gain audio-path RMS, sampled in the
  //                       M/S domain when M/S is on. This is what the
  //                       "Input" meter shows: the level actually feeding
  //                       the spline curve, in the same domain as the
  //                       Output meter. Independent of HP-filter, envelope
  //                       follower, and stereo-link, so the visual
  //                       Output − Input = Gain relationship holds.
  //   mGainMeterSender  — gain reduction in linear amp (dB-converted in
  //                       the widget), drives the "Gain" meter widget.
  //   mOutputLevelSender— post-output-gain wet RMS sampled in the M/S
  //                       domain when M/S is on. The "Output" meter
  //                       visibly drops by the GR amount when the
  //                       compressor cuts a channel.
  //   mLevelMeterSender — env-follower output (post-HP, post-stereo-link),
  //                       i.e. the X coordinate at which the curve is
  //                       evaluated. Routed ONLY to the spline editor's
  //                       "current input" dot — NOT to any meter widget,
  //                       because that data is the wrong domain for a
  //                       user-facing "input level" reading.
  ISender<2> mInputLevelSender;
  ISender<2> mGainMeterSender;
  ISender<2> mOutputLevelSender;
  ISender<2> mLevelMeterSender;

  // Smoothed input + wet RMS² accumulators (per channel) for the Input
  // and Output meters. One-pole filtered at 10 Hz on each ProcessBlock;
  // sqrt at sample-out time gives linear amplitude for the senders.
  std::array<double, 2> mInputLevelRms  { {0.0, 0.0} };
  std::array<double, 2> mOutputLevelRms { {0.0, 0.0} };
#endif

public:
  // Selected-knot state for the side panel. Updated by the spline editor on
  // click / drag and read by Draw to highlight + by SetSelectedKnot to rebind
  // the panel knobs.
  // Default = i=6 (the rightmost of the default-active knots 3..6) so the
  // panel binds to the right-side knee point on first show, which is the
  // more useful starting point for compressor curve editing.
  int mSelectedKnot = 6;
  int mSelectedChannel = 0;

  // Rebind the side-panel knot knobs to the selected knot's params. Called
  // from the spline editor's OnMouseDown.
  void SetSelectedKnot(int knotIdx, int channel);

private:
  // Pointers to the side-panel controls so SetSelectedKnot can rebind them.
  // Populated when mLayoutFunc attaches them; null until then.
  iplug::igraphics::IControl* mKnotPanelKnobX = nullptr;
  iplug::igraphics::IControl* mKnotPanelKnobY = nullptr;
  iplug::igraphics::IControl* mKnotPanelKnobTan = nullptr;
  iplug::igraphics::IControl* mKnotPanelKnobSmoothness = nullptr;
  iplug::igraphics::IControl* mKnotPanelLink = nullptr;
  // Matrix row labels — rewritten "L"/"R" → "M"/"S" when the Mid-Side
  // toggle is on, kept in sync from OnIdle.
  iplug::igraphics::ITextControl* mRowLabelL = nullptr;
  iplug::igraphics::ITextControl* mRowLabelR = nullptr;
  // Meter pointers — same M/S-aware label flip as the matrix row labels,
  // applied via the meter's per-track SetTrackName from OnIdle. All three
  // meters' per-track labels flip together: in M/S mode the DSP processes
  // (mid, side), so each meter's channels are also mid/side.
  iplug::igraphics::IVTrackControlBase* mInputMeter  = nullptr;
  iplug::igraphics::IVTrackControlBase* mGainMeter   = nullptr;
  iplug::igraphics::IVTrackControlBase* mOutputMeter = nullptr;
};
