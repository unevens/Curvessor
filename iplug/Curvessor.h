#pragma once

#include "IPlug_include_in_plug_hdr.h"

// JUCE-free Curvessor DSP + its dependencies (audio-dsp, oversimple, avec).
#include "CurvessorDsp.h"
#include "oversimple/Oversampling.hpp"
#include "avec/Alignment.hpp"
#include "avec/Buffer.hpp"

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
};

using namespace iplug;
using namespace igraphics;

class Curvessor final : public Plugin
{
public:
  Curvessor(const InstanceInfo& info);

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
#endif

#if IPLUG_DSP || IPLUG_EDITOR
  // VU meter snapshots: written from the audio thread, read from the GUI timer.
  std::array<std::atomic<float>, 2> mLevelVuMeterResults{};
  std::array<std::atomic<float>, 2> mGainVuMeterResults{};
#endif
};
