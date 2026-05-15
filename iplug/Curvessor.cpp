#include "Curvessor.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
#include "IControls.h"
#include "SplineEditorDsp.hpp"   // juicy::GuiSpline (JUCE-free scalar eval)
#include "iplug-helpers/controls/Palette.hpp"
#include "iplug-helpers/controls/LightMarkerMeterControl.hpp"
#include "iplug-helpers/controls/SplineEditorControl.hpp"
#include "iplug-helpers/layout/KnobCellLayout.hpp"
#include "iplug-helpers/util/LinkablePairPropagation.hpp"
#include "iplug-helpers/util/SelectedKnotPanel.hpp"
using namespace iplug_helpers;
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

// =============================================================================
// Helpers carried over from Source/Processing.cpp (M/S, gain ramp).
// =============================================================================

namespace {

#if IPLUG_DSP

constexpr double kLn10 = 2.30258509299404568402;
constexpr double kDbToLin = kLn10 / 20.0;

void LeftRightToMidSide(double** io, int n)
{
  for (int i = 0; i < n; ++i) {
    double m = 0.5 * (io[0][i] + io[1][i]);
    double s = 0.5 * (io[0][i] - io[1][i]);
    io[0][i] = m;
    io[1][i] = s;
  }
}

void MidSideToLeftRight(double** io, int n)
{
  for (int i = 0; i < n; ++i) {
    double l = io[0][i] + io[1][i];
    double r = io[0][i] - io[1][i];
    io[0][i] = l;
    io[1][i] = r;
  }
}

void ApplyGain(double** io,
               double* gainTarget,
               double* gainState,
               double alpha,
               int n)
{
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < n; ++i) {
      gainState[c] = gainTarget[c] + alpha * (gainState[c] - gainTarget[c]);
      io[c][i] *= gainState[c];
    }
  }
}

// Symmetric skew shape — matches JUCE's NormalisableRange skewFactor with
// symmetricSkew=true. The midpoint is at normalized=0.5; both halves use
// |d|^(1/skewFactor) so smaller skewFactor → more slider real estate near
// the midpoint.
//
// JUCE convertFrom0to1 reference (symmetric branch):
//   d = 2 * normalized - 1
//   value = mid + halfRange * sign(d) * pow(|d|, 1 / skewFactor)
struct ShapeSymmetricSkew : public IParam::Shape
{
  explicit ShapeSymmetricSkew(double skewFactor) : mSkewFactor(skewFactor) {}

  Shape* Clone() const override { return new ShapeSymmetricSkew(*this); }
  IParam::EDisplayType GetDisplayType() const override { return IParam::kDisplayLinear; }

  double NormalizedToValue(double normalized, const IParam& param) const override
  {
    const double min = param.GetMin();
    const double max = param.GetMax();
    const double mid = 0.5 * (min + max);
    const double halfRange = 0.5 * (max - min);
    const double d = 2.0 * normalized - 1.0;
    const double sign = (d < 0.0) ? -1.0 : 1.0;
    const double skewed = sign * std::pow(std::abs(d), 1.0 / mSkewFactor);
    return mid + halfRange * skewed;
  }

  double ValueToNormalized(double value, const IParam& param) const override
  {
    const double min = param.GetMin();
    const double max = param.GetMax();
    const double mid = 0.5 * (min + max);
    const double halfRange = 0.5 * (max - min);
    if (halfRange == 0.0) return 0.5;
    const double d = (value - mid) / halfRange;
    const double sign = (d < 0.0) ? -1.0 : 1.0;
    const double skewed = sign * std::pow(std::abs(d), mSkewFactor);
    return 0.5 * (skewed + 1.0);
  }

  double mSkewFactor;
};

oversimple::OversamplingSettings MakeInitialOversamplingSettings()
{
  oversimple::OversamplingSettings s;
  s.numUpSampledChannels = 2;
  s.numDownSampledChannels = 2;
  s.upSampleOutputBufferType = oversimple::BufferType::interleaved;
  s.downSampleInputBufferType = oversimple::BufferType::interleaved;
  s.downSampleOutputBufferType = oversimple::BufferType::interleaved;
  s.order = 1;
  s.isUsingLinearPhase = false;
  return s;
}

#endif // IPLUG_DSP

#if IPLUG_EDITOR

// Palette + caption / header / row-label text styles live in
// `iplug-helpers/controls/Palette.hpp` (see `using namespace iplug_helpers`
// at the top of this file). The shared IVStyle ships as a factory
// function so init order is well-defined in this TU (see
// MakePanelStyle's comment in Palette.hpp for why); kCurvessorStyle is
// the canonical TU-local copy, kept under its historical name.
static const IVStyle kCurvessorStyle = iplug_helpers::MakePanelStyle();

// =============================================================================
// CurvessorSplineControl — Curvessor's gain-curve editor. The actual control
// class lives in iplug-helpers/controls/SplineEditorControl.hpp as a template
// parameterised on Plugin + Traits; we instantiate it here with the
// Curvessor-specific data range, knot count, grid ticks, and amp→dB live-dot
// mapping. Behaviour is identical to the previous hand-rolled class: scroll
// to zoom, drag to pan, double-click to toggle enabled/linked, drag knot
// bodies or tangent handles, ghost-render disabled knots, live RMS dot per
// channel, etc.
// =============================================================================
struct CurvessorSplineTraits
{
  static constexpr int    kNumKnots           = 8;
  static constexpr int    kFirstKnotParamBase = kKnot1_enabled;
  static constexpr int    kKnotParamStride    = 10;
  static constexpr int    kMaxKnotsInSpline   = curvessor::maxNumKnots;
  static constexpr double kKnotMin            = -96.0;
  static constexpr double kKnotMax            =   6.0;
  // Anchor at the noise-floor corner — pins the lower-left of the curve so
  // the spline is well-defined for inputs below the lowest editable knot.
  static constexpr double kAnchorX            = -96.0;
  static constexpr double kAnchorY            = -96.0;
  static constexpr std::array<double, 5> kMajorGrid = {
    -96.0, -72.0, -48.0, -24.0, 0.0,
  };
  static constexpr std::array<double, 4> kMinorGrid = {
    -84.0, -60.0, -36.0, -12.0,
  };
  // Curvessor's live-level packet carries the envelope-follower output as
  // a linear amplitude; we plot it on the dB X-axis.
  static double AmpToX(float amp) {
    return 20.0 * std::log10(static_cast<double>(amp));
  }
  static void FormatAxisLabel(double v, char* buf, std::size_t n) {
    std::snprintf(buf, n, "%d", static_cast<int>(v));
  }
  // No symmetry — Curvessor is a compressor, not a waveshaper.
  static bool IsChannelSymmetric(const iplug::IEditorDelegate*, int) {
    return false;
  }
};

using CurvessorSplineControl =
  iplug_helpers::SplineEditorControl<Curvessor, CurvessorSplineTraits>;

// Light-marker meter alias — the class lives in iplug-helpers, kept
// under its historical name in this TU so call sites don't need to
// change.
template<int MAXNC>
using CurvessorMeterControl = iplug_helpers::LightMarkerMeterControl<MAXNC>;

#endif // IPLUG_EDITOR

} // namespace

// =============================================================================
// Plugin construction.
// =============================================================================

Curvessor::Curvessor(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
#if IPLUG_DSP
, mDsp(avec::Aligned<curvessor::Dsp>::make())
, mOversamplingSettings(MakeInitialOversamplingSettings())
, mWetOversampling(mOversamplingSettings)
, mDryOversampling(mOversamplingSettings)
, mSidechainOversampling(mOversamplingSettings)
, mEnvelopeFollowerSettings(mDsp->envelopeFollower)
#endif
{
  // ---------- Globals ----------
  GetParam(kMidSide)->InitBool("Mid-Side", false);
  GetParam(kSideChain)->InitBool("SideChain", false);
  GetParam(kOversampling)->InitEnum("Oversampling", 0,
    {"1x", "2x", "4x", "8x", "16x", "32x"});
  GetParam(kLinearPhaseOversampling)->InitBool("Linear-Phase-Oversampling", false);
  GetParam(kStereoLink)->InitDouble("Stereo-Link", 50.0, 0.0, 100.0, 1.0, "%");
  GetParam(kSmoothingTime)->InitDouble("Smoothing-Time", 50.0, 0.0, 500.0, 1.0, "ms");
  GetParam(kHighPassOrder)->InitEnum("High-Pass-Order", 0,
    {"Disabled", "6dB/Oct", "12db/Oct", "18dB/Oct"});

  // ---------- Linkable float pairs ----------
  // ShapePowCurve(4.0) matches JUCE's skewFactor=0.25 (asymmetric, dense at
  // the bottom). Input/Output gain use ShapeSymmetricSkew(0.25) — defined
  // above; mid-point-dense, both extremes sparse.
  auto initLinkable = [this](int ch0Idx, const char* baseName,
                             double def, double min, double max, double step,
                             const char* unit,
                             const IParam::Shape& shape = IParam::ShapeLinear()) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s_ch0", baseName);
    GetParam(ch0Idx + 0)->InitDouble(buf, def, min, max, step, unit, 0, "", shape);
    std::snprintf(buf, sizeof(buf), "%s_ch1", baseName);
    GetParam(ch0Idx + 1)->InitDouble(buf, def, min, max, step, unit, 0, "", shape);
    std::snprintf(buf, sizeof(buf), "%s_is_linked", baseName);
    GetParam(ch0Idx + 2)->InitBool(buf, true);
  };

  initLinkable(kInputGain_ch0,      "Input-Gain",          0.0, -48.0,   48.0, 0.01, "dB", ShapeSymmetricSkew(0.25));
  initLinkable(kOutputGain_ch0,     "Output-Gain",         0.0, -48.0,   48.0, 0.01, "dB", ShapeSymmetricSkew(0.25));
  initLinkable(kWet_ch0,            "Wet",               100.0,   0.0,  100.0, 1.0,  "%");
  initLinkable(kFeedbackAmount_ch0, "Feedback-Amount",     0.0,   0.0,  100.0, 1.0,  "%");
  initLinkable(kAttack_ch0,         "Attack",             20.0,   0.05, 2000.0, 0.01, "ms", IParam::ShapePowCurve(4.0));
  initLinkable(kRelease_ch0,        "Release",           200.0,   1.0,  2000.0, 0.01, "ms", IParam::ShapePowCurve(4.0));
  initLinkable(kAttackDelay_ch0,    "Attack-Delay",        0.0,   0.0,    25.0, 0.01, "ms");
  initLinkable(kReleaseDelay_ch0,   "Release-Delay",       0.0,   0.0,    25.0, 0.01, "ms");
  initLinkable(kRMSTime_ch0,        "RMS-Time",            0.0,   0.0,  1000.0, 0.01, "ms", IParam::ShapePowCurve(4.0));
  initLinkable(kHighPassCutoff_ch0, "High-Pass-Cutoff",  100.0,  10.0,   250.0, 0.01, "Hz");

  // ---------- Spline knots ----------
  // Defaults match juicy/SplineParameters.cpp: X,Y placed evenly along the
  // x-range at alpha = (i+1)/(N+1) for N=8 editable knots; tangent 1.0;
  // smoothness 1.0; knots 4-7 (i=3..6) enabled, rest disabled. Slot 0 holds
  // the fixed bottom-left anchor; not parameter-backed.
  constexpr int kNumKnots = 8;
  constexpr double kKnotMin = -96.0;
  constexpr double kKnotMax =   6.0;
  constexpr double kKnotRange = kKnotMax - kKnotMin;

  for (int i = 0; i < kNumKnots; ++i) {
    const int n1 = i + 1;
    const int base = kKnot1_enabled + i * 10;
    const bool active = (i >= 3 && i <= 6);
    const double alpha = static_cast<double>(n1) / (kNumKnots + 1);
    const double defXY = kKnotMin + alpha * kKnotRange;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "enabled_k%d", n1);
    GetParam(base + 0)->InitBool(buf, active);
    std::snprintf(buf, sizeof(buf), "linked_k%d", n1);
    GetParam(base + 1)->InitBool(buf, true);

    for (int ch = 0; ch < 2; ++ch) {
      const int chBase = base + 2 + ch * 4;
      std::snprintf(buf, sizeof(buf), "X_k%d_ch%d", n1, ch);
      GetParam(chBase + 0)->InitDouble(buf, defXY, kKnotMin, kKnotMax, 0.01, "dB");
      std::snprintf(buf, sizeof(buf), "Y_k%d_ch%d", n1, ch);
      GetParam(chBase + 1)->InitDouble(buf, defXY, kKnotMin, kKnotMax, 0.01, "dB");
      std::snprintf(buf, sizeof(buf), "Tangent_k%d_ch%d", n1, ch);
      GetParam(chBase + 2)->InitDouble(buf, 1.0, -20.0, 20.0, 0.01);
      std::snprintf(buf, sizeof(buf), "Smoothness_k%d_ch%d", n1, ch);
      GetParam(chBase + 3)->InitDouble(buf, 1.0, 0.0, 1.0, 0.01);
    }
  }

#if IPLUG_DSP
  // VU meter initial values (matches PluginProcessor.cpp).
  mLevelVuMeterResults[0].store(-500.f);
  mLevelVuMeterResults[1].store(-500.f);
  mGainVuMeterResults[0].store(0.f);
  mGainVuMeterResults[1].store(0.f);
#endif

  // Initialize the single preset so it survives PruneUninitializedPresets in
  // the AU entry point, giving hosts a real preset name ("Default") instead
  // of an empty slot. Must come after all params are Init'd —
  // MakeDefaultPreset captures their current values into the preset's chunk
  // via SerializeState.
  //
  // Note: this does NOT silence auval's "Preset name is not retained in
  // retrieved class data" warning. That warning fires because iPlug2's
  // SetState in IPlugAU.cpp:1492 looks up presets by exact name and silently
  // no-ops when auval modifies the name in the saved class data. The
  // workaround would be an override of OnRestoreState / SetState — out of
  // scope for this port pass.
  MakeDefaultPreset("Default", kNumPresets);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    // Layout (1024×640):
    //   - title strip on top
    //   - body band: square spline editor (left), selected-knot column,
    //     then a globals area for the panel buttons
    //   - matrix below the body: 10 linkable params × {L, R, Link} rows
    //   - slim meters at the bottom
    const IRECT bounds = pGraphics->GetBounds();
    const IRECT innerBounds = bounds.GetPadded(-10.f);

    // ---------- Top strip ----------
    const IRECT topStrip = innerBounds.GetFromTop(28);
    const IRECT titleBounds   = topStrip.GetCentredInside(220, 26);
    const IRECT versionBounds = topStrip.GetFromRight(180);

    // ---------- Body / spline editor / VU meter strip ----------
    // The meter strip sits to the right of the spline+side controls, but
    // only spans the spline height (Dario asked for the meters to be
    // shorter than the full body). Below the spline+meter band, the
    // matrix gets the full body width.
    constexpr float kSplineSize    = 380.f;  // was 320 — a bit bigger
    constexpr float kMeterStripW   = 114.f;  // ~50% wider than before
    constexpr float kMeterStripGap = 8.f;

    const IRECT bodyArea = innerBounds.GetReducedFromTop(32);
    const IRECT splineEditorRect = bodyArea.GetFromTop(kSplineSize)
                                            .GetFromLeft(kSplineSize);
    // Upper band = the splineEditor's vertical slice; meters sit at the
    // far right of that slice, NOT below it.
    const IRECT upperBand = bodyArea.GetFromTop(kSplineSize);
    const IRECT meterStrip = upperBand.GetFromRight(kMeterStripW);
    // Three meters split the strip into thirds: Input / Gain / Output
    // (signal-chain order, left to right). Each meter is padded 1 px on
    // each side so the bars don't touch each other.
    constexpr float kMeterCell = kMeterStripW / 3.f;  // 38 px each
    const IRECT inputMeterRect = IRECT(meterStrip.L,
                                        meterStrip.T,
                                        meterStrip.L + kMeterCell,
                                        meterStrip.B).GetPadded(-1, 0, -1, 0);
    const IRECT gainMeterRect  = IRECT(meterStrip.L + kMeterCell,
                                        meterStrip.T,
                                        meterStrip.L + kMeterCell * 2.f,
                                        meterStrip.B).GetPadded(-1, 0, -1, 0);
    const IRECT outputMeterRect = IRECT(meterStrip.L + kMeterCell * 2.f,
                                         meterStrip.T,
                                         meterStrip.R,
                                         meterStrip.B).GetPadded(-1, 0, -1, 0);

    // ---------- Side controls area ----------
    // To the right of the spline, within the spline's vertical range. Two
    // vertical columns of flow-packed controls — Dario asked for a vertical
    // stack that doesn't extend past the spline's bottom, with overflow
    // moving to a new column.
    //   colA (selKnot): X / Y / Tan / Smooth knobs + Link knot toggle.
    //   colB (globals): Mid/Side, Sidechain, Lin Phase, Oversampling,
    //                   HP Order, Stereo Link, Smoothing.
    constexpr float kSideHGap    = 12.f;
    constexpr float kSideColAW   = 76.f;
    constexpr float kSideColBW   = 112.f;  // fits the 108 px Oversampling tab
    constexpr float kSideVGap    = 3.f;
    // Side controls live between the spline editor and the meter strip,
    // within the spline's vertical range.
    const IRECT sideArea = IRECT(splineEditorRect.R + 8.f, splineEditorRect.T,
                                  meterStrip.L - kMeterStripGap, splineEditorRect.B);
    const IRECT sideColA = IRECT(sideArea.L,                          sideArea.T,
                                  sideArea.L + kSideColAW,             sideArea.B);
    const IRECT sideColB = IRECT(sideColA.R + kSideHGap,               sideArea.T,
                                  sideColA.R + kSideHGap + kSideColBW, sideArea.B);

    // Column packers — flow controls top-down with kSideVGap between cells.
    // Selected-knot column is wrapped in a framed box with a 2-line title
    // "Selected Knot" inside (the upstream IVGroupControl label is single-
    // line and overflowed the narrow column, so we paint our own multi-
    // line title via IMultiLineTextControl).
    constexpr float kSelKnotTitleH = 34.f;  // 2 lines of bold 15 pt
    float colAY = sideColA.T + kSelKnotTitleH;
    auto packA = [&](float h) -> IRECT {
      IRECT r(sideColA.L, colAY, sideColA.R, colAY + h);
      colAY += h + kSideVGap;
      return r;
    };
    float colBY = sideColB.T;
    auto packB = [&](float h) -> IRECT {
      IRECT r(sideColB.L, colBY, sideColB.R, colBY + h);
      colBY += h + kSideVGap;
      return r;
    };

    // Knob+label+caption geometry helpers — kKnobDiscW/H, kKnobLabelH,
    // kKnobGap, kKnobCapH, kKnobPairH, kMatrixCapH, and the
    // KnobDiscRectIn / KnobLabelRectIn / KnobCaptionRectIn helpers —
    // live in iplug-helpers/layout/KnobCellLayout.hpp and are pulled in
    // via `using namespace iplug_helpers` at the top of this TU.

    // Selected-knot column (colA): 4 knob+label cells (58 tall = pair
    // height) + a Link knot toggle. Total: 58*4 + 4*4 + 22 = 254 px,
    // comfortably inside the 320 px spline-height budget.
    const IRECT skXRect    = packA(kKnobPairH);
    const IRECT skYRect    = packA(kKnobPairH);
    const IRECT skTanRect  = packA(kKnobPairH);
    const IRECT skSmRect   = packA(kKnobPairH);
    const IRECT skLinkRect = packA(28.f).GetCentredInside(72.f, 28.f);

    // Pre-compute the knot-panel disc rects so both the resize and the
    // first-time-attach branches use identical geometry.
    const IRECT skXDisc   = KnobDiscRectIn(skXRect);
    const IRECT skYDisc   = KnobDiscRectIn(skYRect);
    const IRECT skTanDisc = KnobDiscRectIn(skTanRect);
    const IRECT skSmDisc  = KnobDiscRectIn(skSmRect);

    // Globals column (colB) — Dario's grouping:
    //   1. Sidechain (toggle, first from the top)
    //   2. [Mid/Side toggle + Stereo Link knob] in a framed box
    //   3. [Oversampling menu + Lin Phase toggle] in a framed box
    //   4. HP Order (menu, standalone)
    //   5. Automation Smoothing Time (last, knob with a 2-line label)
    //
    // Budget breakdown (320 px spline-height target, kSideVGap = 1 = 4 gaps):
    //   34 Sidechain + 110 box1 + 80 box2 + 44 HP + 96 Smoothing + 4×3 gaps
    //   = 376 (4 px slack within the 380 px spline-height budget). Sidechain
    //   and Smoothing each get their own IVGroupControl frame, matching the
    //   visual treatment Dario wanted for the inner parameters.
    constexpr float kBoxPad = 2.f;
    constexpr float kBox1H = 110.f;  // 30 toggle + 2 + 74 knob-pair + 4 pad
    constexpr float kBox2H = 80.f;   // 44 menu  + 2 + 30 toggle    + 4 pad
    constexpr float kSideChainBoxH = 34.f;  // 30 toggle + 4 pad
    constexpr float kSmoothLabelH = 36.f;   // 2 lines of bold 15 pt
    constexpr float kSmoothInnerH = kSmoothLabelH + kKnobGap + kKnobDiscH
                                   + kKnobGap + kKnobCapH;       // 92
    constexpr float kSmoothBoxH   = kSmoothInnerH + 2 * kBoxPad;  // 96
    const IRECT sideChainCell = packB(kSideChainBoxH);
    const IRECT box1Cell      = packB(kBox1H);
    const IRECT box2Cell      = packB(kBox2H);
    const IRECT hpCell        = packB(44.f);
    const IRECT smoothingCell = packB(kSmoothBoxH);

    // Cells nested inside the two boxes (padded inset from box bounds).
    const IRECT box1Inner    = box1Cell.GetPadded(-kBoxPad);
    const IRECT midSideCell  = box1Inner.GetFromTop(30.f);
    const IRECT stereoLinkCell = box1Inner.GetReducedFromTop(30.f + 2.f);
    const IRECT box2Inner    = box2Cell.GetPadded(-kBoxPad);
    const IRECT osCell       = box2Inner.GetFromTop(44.f);
    const IRECT linPhaseCell = box2Inner.GetReducedFromTop(44.f + 2.f);

    // ---------- Matrix dimensions (compact) ----------
    // Fixed cell dimensions and small fixed gaps — the matrix is sized from
    // its content (top-left anchored to the spline editor's left edge, just
    // below the spline). Inter-row gaps are ~6 px (down from ~30) and
    // inter-column gaps ~10 px (down from ~60), per Dario's tighter-spacing
    // request.
    constexpr int   kNumLinkables   = 10;
    // Header is 2 lines tall (15 pt bold) so spelled-out column names like
    // "Input Gain" / "Attack Delay" wrap onto two short lines.
    constexpr float kMHeaderH       = 36.f;
    constexpr float kMKnobRowH      = 55.f;  // 36 disc + 1 gap + 18 caption
    constexpr float kMLinkRowH      = 26.f;
    constexpr float kMRowGap        = 6.f;
    constexpr float kMLinkGap       = 4.f;
    constexpr float kMHeaderRowGap  = 0.f;  // tighten header→knob-row gap
    constexpr float kMColW          = 62.f;  // fits 15 pt captions with " ms" / " dB" suffix
    constexpr float kMColGap        = 4.f;
    constexpr float kMRowLabelW     = 44.f;  // fits "Right" / "Side" at 15 pt bold
    constexpr float kMRowLabelGap   = 4.f;
    const float matrixContentH = kMHeaderH + kMHeaderRowGap
                               + kMKnobRowH + kMRowGap
                               + kMKnobRowH + kMLinkGap
                               + kMLinkRowH;
    const float matrixContentW = kMRowLabelW + kMRowLabelGap
                               + kNumLinkables * kMColW
                               + (kNumLinkables - 1) * kMColGap;

    // Matrix sits directly below the spline editor, left-aligned with it
    // (Dario asked for "just below the spline panel, aligned to the left").
    // Gap between the spline editor and the matrix, comparable to the
    // 10 px window-edge padding so the matrix doesn't feel glued to the
    // spline's bottom edge.
    constexpr float kMatrixTopGap = 18.f;
    const IRECT matrixBand       = bodyArea.GetReducedFromTop(kSplineSize
                                                                + kMatrixTopGap);
    const IRECT matrixContent    = IRECT(splineEditorRect.L, matrixBand.T,
                                          splineEditorRect.L + matrixContentW,
                                          matrixBand.T + matrixContentH);
    const IRECT matrixHeaderArea = matrixContent.GetFromTop(kMHeaderH);
    const IRECT matrixRows       = matrixContent.GetReducedFromTop(kMHeaderH
                                                                  + kMHeaderRowGap);
    const IRECT rowLabelCol      = matrixRows.GetFromLeft(kMRowLabelW);
    const IRECT paramColsArea    = matrixRows.GetReducedFromLeft(kMRowLabelW
                                                                + kMRowLabelGap);

    const float lRowT    = paramColsArea.T;
    const float rRowT    = lRowT + kMKnobRowH + kMRowGap;
    const float linkRowT = rRowT + kMKnobRowH + kMLinkGap;
    auto colX = [&](int idx) {
      return paramColsArea.L + idx * (kMColW + kMColGap);
    };
    auto headerRect = [&](int idx) {
      return IRECT(colX(idx), matrixHeaderArea.T,
                   colX(idx) + kMColW, matrixHeaderArea.B);
    };
    auto matrixCell = [&](int colIdx, int rowIdx) {
      float rowT = lRowT, rowH = kMKnobRowH;
      if (rowIdx == 1)      { rowT = rRowT; }
      else if (rowIdx == 2) { rowT = linkRowT; rowH = kMLinkRowH; }
      return IRECT(colX(colIdx), rowT, colX(colIdx) + kMColW, rowT + rowH);
    };
    const IRECT lblLRect    = IRECT(rowLabelCol.L, lRowT,
                                    rowLabelCol.R, lRowT + kMKnobRowH);
    const IRECT lblRRect    = IRECT(rowLabelCol.L, rRowT,
                                    rowLabelCol.R, rRowT + kMKnobRowH);
    const IRECT lblLinkRect = IRECT(rowLabelCol.L, linkRowT,
                                    rowLabelCol.R, linkRowT + kMLinkRowH);

    // ---------- Resize-relayout branch ----------
    if (pGraphics->NControls()) {
      pGraphics->GetBackgroundControl()->SetTargetAndDrawRECTs(bounds);
      pGraphics->GetControlWithTag(kCtrlTagTitle)->SetTargetAndDrawRECTs(titleBounds);
      pGraphics->GetControlWithTag(kCtrlTagVersionNumber)->SetTargetAndDrawRECTs(versionBounds);
      pGraphics->GetControlWithTag(kCtrlTagSplineEditor)->SetTargetAndDrawRECTs(splineEditorRect);
      pGraphics->GetControlWithTag(kCtrlTagInputMeter)->SetTargetAndDrawRECTs(inputMeterRect);
      pGraphics->GetControlWithTag(kCtrlTagGainMeter)->SetTargetAndDrawRECTs(gainMeterRect);
      pGraphics->GetControlWithTag(kCtrlTagOutputMeter)->SetTargetAndDrawRECTs(outputMeterRect);
      if (mKnotPanelKnobX)          mKnotPanelKnobX         ->SetTargetAndDrawRECTs(skXDisc);
      if (mKnotPanelKnobY)          mKnotPanelKnobY         ->SetTargetAndDrawRECTs(skYDisc);
      if (mKnotPanelKnobTan)        mKnotPanelKnobTan       ->SetTargetAndDrawRECTs(skTanDisc);
      if (mKnotPanelKnobSmoothness) mKnotPanelKnobSmoothness->SetTargetAndDrawRECTs(skSmDisc);
      if (mKnotPanelLink)           mKnotPanelLink          ->SetTargetAndDrawRECTs(skLinkRect);
      // (matrix cells and globals don't have tags right now — they stay at
      // their initial positions on resize.)
      return;
    }

    // ---------- First-time attach ----------
    pGraphics->SetLayoutOnResize(true);
    pGraphics->AttachCornerResizer(EUIResizerMode::Size, true);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->LoadFont("Roboto-Bold", ROBOTO_BOLD_FN);
    pGraphics->AttachPanelBackground(kPanelBg);

    pGraphics->AttachControl(
      new ITextControl(titleBounds, "CURVESSOR", kTitleText),
      kCtrlTagTitle);
    WDL_String buildInfoStr;
    GetBuildInfoStr(buildInfoStr, __DATE__, __TIME__);
    pGraphics->AttachControl(
      new ITextControl(versionBounds, buildInfoStr.Get(), kVersionText),
      kCtrlTagVersionNumber);

    pGraphics->AttachControl(
      new CurvessorSplineControl(splineEditorRect),
      kCtrlTagSplineEditor);

    // Styles shared by selected-knot column, globals, and matrix.
    // - knobStyleNoLabel: matrix-size disc (36 px), no built-in label. Side
    //   panels add their own bold ITextControl directly above (via the
    //   KnobLabelRectIn helper) to keep the disc visually identical to the
    //   matrix knobs (Dario explicitly asked for this).
    // - toggleNameInButtonStyle: switch with no external label; the param
    //   name is drawn *inside* the button via offText/onText (both set to
    //   the same string), so state is conveyed by color alone (kPR fill
    //   when on).
    // ShowValue(false) — the value text used to live inside/next to the
    // disc, but it's now drawn by a separate editable ICaptionControl below
    // each knob (see kCaptionText / attachLabeledKnob, attachMatrixKnob).
    const IVStyle knobStyleNoLabel = kCurvessorStyle.WithShowLabel(false)
                                                    .WithShowValue(false)
                                                    .WithFrameThickness(0.f);
    // Editable captions sitting just below each knob disc. Click → text-
    // entry box. Both side-panel and matrix captions render at 15 pt so
    // the LCD-style readouts look uniform; the matrix cells were widened
    // (kMColW 36 → 44) to fit values like "-36.0" or "127.5" at this size.
    static const IText kSideCaptionText(15, IColor(255, 200, 220, 230),
                                        nullptr, EAlign::Center);
    static const IText kMatrixCaptionText(15, IColor(255, 200, 220, 230),
                                          nullptr, EAlign::Center);
    // Caption boxes use a transparent fill — the panel background shows
    // through and there's no extra visual chrome around each knob.
    static const IColor kCaptionBg(0, 0, 0, 0);
    const IVStyle toggleNameInButtonStyle = kCurvessorStyle
        .WithShowLabel(false)
        .WithValueText(IText(15, IColor(255, 220, 230, 235),
                             "Roboto-Bold", EAlign::Center));

    // Bold name label + 36 px disc + editable caption, all centred inside
    // `cell`. Returns the knob's IControl* so callers can keep handles for
    // dynamic param rebinding (selected-knot column). Body lives in
    // iplug-helpers/layout/KnobCellLayout.hpp; this lambda just binds the
    // Curvessor-specific styles.
    auto attachLabeledKnob =
      [&](const IRECT& cell, int paramIdx, const char* name) -> IControl* {
        return AttachLabeledKnob(pGraphics, cell, paramIdx, name,
                                  kCurvessorStyle.labelText, knobStyleNoLabel,
                                  kSideCaptionText, kCaptionBg);
      };

    // Selected-knot column — empty-label group frame plus a separate
    // multi-line title at the top of the column. packA was offset by
    // kSelKnotTitleH at colAY initialisation so the X/Y/Tan/Smooth knobs
    // start below the title.
    pGraphics->AttachControl(new IVGroupControl(
      sideColA, "", 0.f, kCurvessorStyle.WithFrameThickness(1.f)));
    pGraphics->AttachControl(new IMultiLineTextControl(
      sideColA.GetFromTop(kSelKnotTitleH).GetPadded(-2, 2, -2, 0),
      "Selected Knot", kCurvessorStyle.labelText, kCaptionBg));
    {
      const int base = kKnot1_enabled + mSelectedKnot * 10;
      const int chBase = base + 2 + mSelectedChannel * 4;
      mKnotPanelKnobX          = attachLabeledKnob(skXRect,   chBase + 0, "X");
      mKnotPanelKnobY          = attachLabeledKnob(skYRect,   chBase + 1, "Y");
      mKnotPanelKnobTan        = attachLabeledKnob(skTanRect, chBase + 2, "Tan");
      mKnotPanelKnobSmoothness = attachLabeledKnob(skSmRect,  chBase + 3, "Smooth");
      // "Link" is drawn inside the button via the value text (offText/onText),
      // so state shows via color only — same convention as the three globals
      // toggles below.
      mKnotPanelLink = pGraphics->AttachControl(
        new IVToggleControl(skLinkRect, base + 1, "", toggleNameInButtonStyle,
                            "Link", "Link"));
    }

    // ---------- Globals column (colB) ----------
    // Every cell here gets its own framed box for visual consistency.
    const IVStyle boxStyle = kCurvessorStyle.WithFrameThickness(1.f);

    // Sidechain (boxed, top of colB).
    pGraphics->AttachControl(new IVGroupControl(sideChainCell, "", 0.f, boxStyle));
    pGraphics->AttachControl(new IVToggleControl(
      sideChainCell.GetCentredInside(96, 30), kSideChain, "",
      toggleNameInButtonStyle, "Sidechain", "Sidechain"));

    // Box 1: Mid/Side + Stereo Link.
    pGraphics->AttachControl(new IVGroupControl(box1Cell, "", 0.f, boxStyle));
    pGraphics->AttachControl(new IVToggleControl(
      midSideCell.GetCentredInside(96, 30), kMidSide, "",
      toggleNameInButtonStyle, "Mid/Side", "Mid/Side"));
    attachLabeledKnob(stereoLinkCell, kStereoLink, "Stereo Link");

    // Box 2: Oversampling + Lin Phase.
    pGraphics->AttachControl(new IVGroupControl(box2Cell, "", 0.f, boxStyle));
    pGraphics->AttachControl(new IVMenuButtonControl(
      osCell.GetCentredInside(96, 44), kOversampling, "Oversampling",
      kCurvessorStyle));
    pGraphics->AttachControl(new IVToggleControl(
      linPhaseCell.GetCentredInside(96, 30), kLinearPhaseOversampling, "",
      toggleNameInButtonStyle, "Lin Phase", "Lin Phase"));

    // HP Order standalone — IVMenuButtonControl with its own param-name
    // label above the button.
    pGraphics->AttachControl(new IVMenuButtonControl(
      hpCell.GetCentredInside(96, 44), kHighPassOrder, "HP Order",
      kCurvessorStyle));

    // Automation Smoothing Time — boxed, multi-line bold label + 36 px
    // disc + 15 pt editable caption. Param name is too long for a single
    // line at this column width; IMultiLineTextControl auto-wraps it.
    pGraphics->AttachControl(new IVGroupControl(smoothingCell, "", 0.f, boxStyle));
    {
      const IRECT smoothingInner = smoothingCell.GetPadded(-kBoxPad);
      const IRECT lblRect  = smoothingInner.GetFromTop(kSmoothLabelH);
      const float discT    = smoothingInner.T + kSmoothLabelH + kKnobGap;
      const float cx       = smoothingInner.MW();
      const IRECT discRect = IRECT(cx - kKnobDiscW * 0.5f, discT,
                                    cx + kKnobDiscW * 0.5f, discT + kKnobDiscH);
      const IRECT capRect  = IRECT(smoothingInner.L, discRect.B + kKnobGap,
                                    smoothingInner.R, discRect.B + kKnobGap
                                                       + kKnobCapH);
      pGraphics->AttachControl(new IMultiLineTextControl(
        lblRect, "Automation Smoothing Time", kCurvessorStyle.labelText,
        kCaptionBg));
      pGraphics->AttachControl(new IVKnobControl(
        discRect, kSmoothingTime, "", knobStyleNoLabel));
      pGraphics->AttachControl(new ICaptionControl(
        capRect, kSmoothingTime, kSideCaptionText, kCaptionBg,
        /*showParamLabel=*/true));
    }

    // ---------- Matrix ----------
    // Full names — IMultiLineTextControl wraps the longer ones onto two
    // lines so each column header still fits in the kMColW (62 px) cell.
    struct LinkableSpec { const char* name; int ch0Idx; };
    static const LinkableSpec linkables[kNumLinkables] = {
      { "Input Gain",     kInputGain_ch0      },
      { "Output Gain",    kOutputGain_ch0     },
      { "Wet",            kWet_ch0            },
      { "Feedback",       kFeedbackAmount_ch0 },
      { "Attack",         kAttack_ch0         },
      { "Release",        kRelease_ch0        },
      { "Attack Delay",   kAttackDelay_ch0    },
      { "Release Delay",  kReleaseDelay_ch0   },
      { "RMS Time",       kRMSTime_ch0        },
      { "High-Pass",      kHighPassCutoff_ch0 },
    };

    // Column-header text style and row-label text style — bold so they read
    // first at the tight row/column spacing, low-contrast so they sit back
    // visually like instrumentation labels.
    static const IText kMatrixHeaderText(15, IColor(255, 200, 220, 230),
                                         "Roboto-Bold", EAlign::Center);
    static const IText kRowLabelText(15, IColor(255, 200, 220, 230),
                                     "Roboto-Bold", EAlign::Center);
    const IVStyle matrixToggleStyle = kCurvessorStyle.WithShowLabel(false);

    for (int i = 0; i < kNumLinkables; ++i) {
      const auto& lp = linkables[i];

      // Multi-line header — auto-wraps "Input Gain" → "Input\nGain" etc.
      pGraphics->AttachControl(
        new IMultiLineTextControl(headerRect(i), lp.name, kMatrixHeaderText,
                                   kCaptionBg));

      // Each knob cell (36×46) splits into a 32×32 disc on top and a 13 px
      // editable caption below — kKnobGap of 1 px between them. The Link
      // toggle row stays 36×22 with no caption.
      auto splitKnobCell = [&](const IRECT& cell) {
        const IRECT disc = cell.GetFromTop(kKnobDiscH)
                               .GetCentredInside(kKnobDiscW, kKnobDiscH);
        const IRECT cap  = cell.GetFromBottom(kMatrixCapH);
        return std::make_pair(disc, cap);
      };
      const auto [lDisc, lCap] = splitKnobCell(matrixCell(i, 0));
      const auto [rDisc, rCap] = splitKnobCell(matrixCell(i, 1));
      const IRECT lnkBtn = matrixCell(i, 2);

      pGraphics->AttachControl(new IVKnobControl(lDisc, lp.ch0Idx + 0, "", knobStyleNoLabel));
      pGraphics->AttachControl(new ICaptionControl(lCap, lp.ch0Idx + 0, kMatrixCaptionText, kCaptionBg, /*showParamLabel=*/true));
      pGraphics->AttachControl(new IVKnobControl(rDisc, lp.ch0Idx + 1, "", knobStyleNoLabel));
      pGraphics->AttachControl(new ICaptionControl(rCap, lp.ch0Idx + 1, kMatrixCaptionText, kCaptionBg, /*showParamLabel=*/true));
      // Use IVToggleControl (not IVSwitchControl) so the button fill colour
      // tracks the on/off *state* — matching the Mid/Side / Sidechain /
      // Lin Phase pattern. The empty offText/onText leave the button
      // textless; the row label "Link" on the left of the matrix and the
      // column header identify which param it links.
      pGraphics->AttachControl(new IVToggleControl(lnkBtn, lp.ch0Idx + 2, "",
                                                   matrixToggleStyle, "", ""));
    }

    // Row labels. "L" / "R" become "M" / "S" in mid/side mode — OnIdle
    // syncs the text based on kMidSide state.
    {
      auto* lblL = new ITextControl(lblLRect, "Left", kRowLabelText);
      auto* lblR = new ITextControl(lblRRect, "Right", kRowLabelText);
      auto* lblK = new ITextControl(lblLinkRect, "Link", kRowLabelText);
      pGraphics->AttachControl(lblL);
      pGraphics->AttachControl(lblR);
      pGraphics->AttachControl(lblK);
      mRowLabelL = lblL;
      mRowLabelR = lblR;
    }

    // Three meters — vertical, on the right edge of the plug, arranged in
    // signal-chain order: Input level (pre-compression envelope), Gain
    // reduction (with SetBaseValue(0.5) so the fill grows up from centre
    // for boost / down for cut), Output level (post-compression wet RMS).
    //
    // The Input meter shares its data with the spline editor's "current
    // input" dot via OnIdle's TransmitDataToControlsWithTags — it's the
    // X-axis coordinate of where on the curve the signal currently sits.
    // The Output meter is the one that drops 24 dB when the compressor
    // cuts a channel by 24 dB; it's sampled post-wet/dry/output-gain in
    // M/S domain (when M/S is on, before MidSideToLeftRight), so its
    // per-track labels flip to M/S in M/S mode like the Input meter.
    const IVStyle meterStyle = kCurvessorStyle.WithDrawFrame(false);
    auto* inputMeter = static_cast<CurvessorMeterControl<2>*>(pGraphics->AttachControl(
      new CurvessorMeterControl<2>(inputMeterRect, "Input", meterStyle,
                                    EDirection::Vertical, {"L","R"}, 0,
                                    CurvessorMeterControl<2>::EResponse::Log,
                                    -60.f, 6.f),
      kCtrlTagInputMeter));
    auto* gainMeter = static_cast<CurvessorMeterControl<2>*>(pGraphics->AttachControl(
      new CurvessorMeterControl<2>(gainMeterRect, "Gain", meterStyle,
                                    EDirection::Vertical, {"L","R"}, 0,
                                    CurvessorMeterControl<2>::EResponse::Log,
                                    -36.f, 36.f,
                                    {-24, -12, -6, 0, 6, 12, 24}),
      kCtrlTagGainMeter));
    gainMeter->SetBaseValue(0.5);
    auto* outputMeter = static_cast<CurvessorMeterControl<2>*>(pGraphics->AttachControl(
      new CurvessorMeterControl<2>(outputMeterRect, "Output", meterStyle,
                                    EDirection::Vertical, {"L","R"}, 0,
                                    CurvessorMeterControl<2>::EResponse::Log,
                                    -60.f, 6.f),
      kCtrlTagOutputMeter));
    // Cache the meters so OnIdle can flip their per-track labels between
    // "L"/"R" and "M"/"S" when the Mid-Side toggle changes — meter values
    // are already in the M/S domain in that mode because the DSP signal
    // chain processes in M/S before the LR-restore at the end of
    // ProcessBlock.
    mInputMeter  = inputMeter;
    mGainMeter   = gainMeter;
    mOutputMeter = outputMeter;
  };
#endif
}

#if IPLUG_DSP

// =============================================================================
// OnReset — analog of JUCE's prepareToPlay + reset.
// =============================================================================
void Curvessor::OnReset()
{
  const int blockSize = GetBlockSize();

  {
    std::lock_guard<std::recursive_mutex> guard(mOversamplingMutex);
    EnsureOversamplingCurrent();
    mOversamplingSettings.maxNumInputSamples = static_cast<uint32_t>(blockSize);
    mWetOversampling.prepareBuffers(static_cast<uint32_t>(blockSize));
    mDryOversampling.prepareBuffers(static_cast<uint32_t>(blockSize));
    mSidechainOversampling.prepareBuffers(static_cast<uint32_t>(blockSize));
  }

  mDryBuffer.setNumSamples(blockSize);
  mSidechainScratch.setNumSamples(blockSize);

  // Always report the current latency on reset — covers the case where
  // EnsureOversamplingCurrent above early-returned because the cached
  // last-applied settings happened to match the current params (typical
  // on first launch).
  SetLatency(static_cast<int>(mWetOversampling.getLatency()));

  // Reset DSP state and seed gain ramps from the current parameter values.
  // Mirrors CurvessorAudioProcessor::resetDsp().
  UpdateSplineFromParams();
  mDsp->envelopeFollower.reset();
  mDsp->autoSpline.reset();

  const double stereoLinkTarget = 0.01 * GetParam(kStereoLink)->Value();

  for (int c = 0; c < 2; ++c) {
    mDsp->gainVuMeterBuffer[c] = 0.0;
    mDsp->levelVuMeterBuffer[c] = -200.0;
    mInputLevelRms[c]  = 0.0;
    mOutputLevelRms[c] = 0.0;
    mDsp->stereoLink[c] = stereoLinkTarget;
    mDsp->inputGain[c]  = std::exp(kDbToLin * GetLinkable(kInputGain_ch0, c));
    mDsp->outputGain[c] = std::exp(kDbToLin * GetLinkable(kOutputGain_ch0, c));
    mDsp->wetAmount[c]  = 0.01 * GetLinkable(kWet_ch0, c);
    mDsp->sidechainInputGain[c] = mDsp->inputGain[c];
    mDsp->feedbackAmount[c] = mDsp->feedbackAmountTarget[c] =
      GetLinkable(kFeedbackAmount_ch0, c);
  }
}

// =============================================================================
// Spline update — replicates juicy/SplineParameters::updateSpline(AutoSpline&).
// =============================================================================
int Curvessor::UpdateSplineFromParams()
{
  auto& splineKnots = mDsp->autoSpline.spline.settings.knots;
  auto& automationKnots = mDsp->autoSpline.automator.knots;

  int n = 0;

  // Single fixed anchor at (-96, -96, t=1, s=1) — mirrors the lone entry in
  // SplineParameters' fixedKnots list on the JUCE side.
  for (int c = 0; c < 2; ++c) {
    automationKnots[n].x[c] = splineKnots[n].x[c] = -96.0;
    automationKnots[n].y[c] = splineKnots[n].y[c] = -96.0;
    automationKnots[n].t[c] = splineKnots[n].t[c] = 1.0;
    automationKnots[n].s[c] = splineKnots[n].s[c] = 1.0;
  }
  ++n;

  constexpr int kNumKnots = 8;
  for (int i = 0; i < kNumKnots; ++i) {
    const int base = kKnot1_enabled + i * 10;
    const bool enabled = GetParam(base + 0)->Bool();
    if (!enabled) continue;

    const bool linked = GetParam(base + 1)->Bool();

    for (int c = 0; c < 2; ++c) {
      const int srcCh = linked ? 0 : c;
      const int chBase = base + 2 + srcCh * 4;
      automationKnots[n].x[c] = GetParam(chBase + 0)->Value();
      automationKnots[n].y[c] = GetParam(chBase + 1)->Value();
      automationKnots[n].t[c] = GetParam(chBase + 2)->Value();
      automationKnots[n].s[c] = GetParam(chBase + 3)->Value();
    }
    ++n;
  }

  return n;
}

// =============================================================================
// Oversampling reconfig — flush + rebuild if user moved Oversampling /
// Linear-Phase-Oversampling.
// =============================================================================
void Curvessor::EnsureOversamplingCurrent()
{
  const int order = GetParam(kOversampling)->Int();           // 0..5 → 1x..32x
  const bool linPhase = GetParam(kLinearPhaseOversampling)->Bool();

  if (order == mLastOversamplingOrder && linPhase == mLastOversamplingLinearPhase) {
    return;
  }

  mOversamplingSettings.order = order;
  mOversamplingSettings.isUsingLinearPhase = linPhase;

  // TOversampling internally no-ops setOrder / setUseLinearPhase if the new
  // value matches the current one, and resets state when it changes.
  const uint32_t uOrder = static_cast<uint32_t>(order);
  mWetOversampling.setOrder(uOrder);
  mWetOversampling.setUseLinearPhase(linPhase);
  mDryOversampling.setOrder(uOrder);
  mDryOversampling.setUseLinearPhase(linPhase);
  mSidechainOversampling.setOrder(uOrder);
  mSidechainOversampling.setUseLinearPhase(linPhase);

  mLastOversamplingOrder = order;
  mLastOversamplingLinearPhase = linPhase;

  // Report the new latency so the host can do PDC. Linear-phase FIR
  // oversampling adds substantial latency; minimum-phase IIR reports 0
  // (group delay isn't constant so iPlug2 can't expose it as a single
  // sample count). The per-format SetLatency override notifies the host:
  // VST3 calls restartComponent(kLatencyChanged), CLAP defers via
  // runOnMainThread, AU informs its listeners.
  SetLatency(static_cast<int>(mWetOversampling.getLatency()));
}

// =============================================================================
// ProcessBlock — port of Source/Processing.cpp.
// =============================================================================
void Curvessor::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  static_assert(std::is_same_v<sample, double>,
                "Curvessor's DSP expects PLUG_SAMPLE_DST = double");

  const int nInChans = NInChansConnected();
  const bool isSideChainAvailable = (nInChans >= 4);
  const bool isMidSide = GetParam(kMidSide)->Bool();
  const bool isSideChainRequested = GetParam(kSideChain)->Bool();
  const bool isUsingSideChain = isSideChainRequested && isSideChainAvailable;

  // iPlug2 passes input + output buffers separately. Curvessor's chain is
  // in-place over a single buffer of 4 channels (main 0..1, sc 2..3). Copy
  // main inputs to outputs up-front so the chain can mutate outputs[] in
  // place. SC reads from inputs[2..3] directly.
  for (int c = 0; c < 2; ++c) {
    std::copy(inputs[c], inputs[c] + nFrames, outputs[c]);
  }

  double* ioAudio[2] = { outputs[0], outputs[1] };

  // Apply oversampling-config changes that landed since last block.
  {
    std::lock_guard<std::recursive_mutex> guard(mOversamplingMutex);
    EnsureOversamplingCurrent();
  }

  // ---------- pull parameters → DSP-targets ----------
  mDsp->stereoLinkTarget = 0.01 * GetParam(kStereoLink)->Value();

  const double sampleRate = GetSampleRate();
  const double invUpsampledSampleRate =
    1.0 / (sampleRate * mWetOversampling.getOversamplingRate());
  const double bltFrequencyCoef = M_PI * invUpsampledSampleRate;
  const double upsampledAngularFrequencyCoef =
    1000.0 * 2.0 * M_PI * invUpsampledSampleRate;

  const float smoothingTime = static_cast<float>(GetParam(kSmoothingTime)->Value());
  const double automationAlpha =
    smoothingTime == 0.f
      ? 0.0
      : std::exp(-upsampledAngularFrequencyCoef / smoothingTime);
  mDsp->automationAlpha = automationAlpha;

  double inputGainTarget[2];
  double outputGainTarget[2];
  double wetAmountTarget[2];

  for (int c = 0; c < 2; ++c) {
    inputGainTarget[c]  = std::exp(kDbToLin * GetLinkable(kInputGain_ch0, c));
    outputGainTarget[c] = std::exp(kDbToLin * GetLinkable(kOutputGain_ch0, c));
    wetAmountTarget[c]  = 0.01 * GetLinkable(kWet_ch0, c);
    mDsp->feedbackAmountTarget[c] = 0.01 * GetLinkable(kFeedbackAmount_ch0, c);

    const double hpCutoff = GetLinkable(kHighPassCutoff_ch0, c);
    const double g = std::tan(bltFrequencyCoef * hpCutoff);
    mDsp->highPassCoef[c] = g / (1.0 + g);

    const float rmsTime = static_cast<float>(GetLinkable(kRMSTime_ch0, c));
    const double rmsAlpha =
      rmsTime == 0.f ? 0.0
                     : std::exp(-upsampledAngularFrequencyCoef / rmsTime);
    const double attackFreq = GetLinkable(kAttack_ch0, c);
    const double releaseFreq =
      upsampledAngularFrequencyCoef / GetLinkable(kRelease_ch0, c);
    const double attackDelay  = 0.01 * GetLinkable(kAttackDelay_ch0, c);
    const double releaseDelay = 0.01 * GetLinkable(kReleaseDelay_ch0, c);
    mEnvelopeFollowerSettings.setup(c, rmsAlpha, attackFreq, releaseFreq,
                                    attackDelay, releaseDelay);
  }

  mDsp->autoSpline.automator.setSmoothingAlpha(automationAlpha);

  const int numActiveKnots = UpdateSplineFromParams();

  // Bypass detection (matches JUCE side).
  const bool isWetPassNeeded = [&] {
    double m = wetAmountTarget[0] * wetAmountTarget[1] *
               mDsp->wetAmount[0]  * mDsp->wetAmount[1];
    if (m == 1.0) return false;
    if (m == 0.0) {
      return !(wetAmountTarget[0] == 0.0 && wetAmountTarget[1] == 0.0 &&
               mDsp->wetAmount[0]  == 0.0 && mDsp->wetAmount[1]  == 0.0);
    }
    return true;
  }();

  const bool isBypassing =
    (!isWetPassNeeded && (mDsp->wetAmount[0] == 0.0)) || (numActiveKnots == 0);

  const int highPassOrder = GetParam(kHighPassOrder)->Int();

  // ---------- signal chain ----------
  if (isMidSide) {
    LeftRightToMidSide(ioAudio, nFrames);
  }

  // Snapshot dry.
  mDryBuffer.setNumSamples(nFrames);
  for (int c = 0; c < 2; ++c) {
    std::copy(ioAudio[c], ioAudio[c] + nFrames, mDryBuffer.get()[c]);
  }

  ApplyGain(ioAudio, inputGainTarget, mDsp->inputGain, automationAlpha, nFrames);

  // Input-level RMS: 10 Hz one-pole over (M/S-domain when M/S is on)
  // ioAudio AFTER input gain, BEFORE upsampling / HP-filter / envelope-
  // follower / stereo-link. This is the actual signal level feeding the
  // spline curve in the same domain as the Output meter, so the user
  // can read Output − Input = Gain off the three bars regardless of HP
  // engagement or stereo-link blending (which only affect the curve
  // lookup, not the audio path).
  {
    constexpr double kVuMeterFrequency = 10.0;
    const double a = std::exp(-2.0 * M_PI * kVuMeterFrequency / sampleRate);
    for (int c = 0; c < 2; ++c) {
      double rms = mInputLevelRms[c];
      for (int i = 0; i < nFrames; ++i) {
        const double s = ioAudio[c][i];
        rms = a * (rms - s * s) + s * s;
      }
      mInputLevelRms[c] = rms;
    }
  }

  const uint32_t numInputSamples = static_cast<uint32_t>(nFrames);
  mWetOversampling.prepareBuffers(numInputSamples);
  mDryOversampling.prepareBuffers(numInputSamples);
  mSidechainOversampling.prepareBuffers(numInputSamples);

  const uint32_t numUpsampledSamples =
    mWetOversampling.upSample(ioAudio, numInputSamples);
  mDryOversampling.upSample(mDryBuffer.get(), numInputSamples);

  if (numUpsampledSamples == 0) {
    for (int c = 0; c < 2; ++c) {
      std::fill(outputs[c], outputs[c] + nFrames, 0.0);
    }
    return;
  }

  auto& upsampledBuffer = mWetOversampling.getUpSampleOutputInterleaved();
  auto& upsampledIo = upsampledBuffer.getBuffer2(0);
  auto& upsampledDryBuffer = mDryOversampling.getUpSampleOutputInterleaved();

  // Sidechain envelope-input prep. Uses the member scratch buffer (sized in
  // OnReset to GetBlockSize()) so we never touch the stack.
  double* envelopeInput[2];
  if (isUsingSideChain) {
    std::copy(inputs[2], inputs[2] + nFrames, mSidechainScratch.get()[0]);
    std::copy(inputs[3], inputs[3] + nFrames, mSidechainScratch.get()[1]);
    envelopeInput[0] = mSidechainScratch.get()[0];
    envelopeInput[1] = mSidechainScratch.get()[1];
    if (isMidSide) LeftRightToMidSide(envelopeInput, nFrames);
    ApplyGain(envelopeInput, inputGainTarget, mDsp->sidechainInputGain,
              automationAlpha, nFrames);
  } else {
    envelopeInput[0] = ioAudio[0];
    envelopeInput[1] = ioAudio[1];
  }
  mSidechainOversampling.upSample(envelopeInput, numInputSamples);

  auto& upsampledSideChainInput =
    mSidechainOversampling.getUpSampleOutputInterleaved().getBuffer2(0);

  const bool isFeedbackNeeded = [&] {
    for (int c = 0; c < 2; ++c) {
      if (mDsp->feedbackAmountTarget[c] > 0.0) return true;
      if (mDsp->feedbackAmount[c] > 0.0) return true;
    }
    return false;
  }();

  if (!isBypassing) {
    if (isSideChainRequested) {
      if (isSideChainAvailable) {
        mDsp->sidechainProcess(upsampledIo, upsampledSideChainInput,
                               numActiveKnots, highPassOrder);
      }
    } else if (isFeedbackNeeded) {
      mDsp->feedbackProcess(upsampledIo, numActiveKnots, highPassOrder);
    } else {
      mDsp->forwardProcess(upsampledIo, numActiveKnots, highPassOrder);
    }
  }

  mWetOversampling.downSample(upsampledBuffer, numInputSamples);
  mDryOversampling.downSample(upsampledDryBuffer, numInputSamples);

  auto& wetOutput = mWetOversampling.getDownSampleOutputInterleaved();
  auto& dryOutput = mDryOversampling.getDownSampleOutputInterleaved();

  if (isWetPassNeeded) {
    auto& dryBuf = dryOutput.getBuffer2(0);
    auto& wetBuf = wetOutput.getBuffer2(0);

    Vec2d alpha = Vec2d(automationAlpha);
    Vec2d amount = Vec2d().load_a(mDsp->wetAmount);
    Vec2d amountTarget = Vec2d().load(wetAmountTarget);
    Vec2d gain = Vec2d().load_a(mDsp->outputGain);
    Vec2d gainTarget = Vec2d().load(outputGainTarget);

    for (int i = 0; i < nFrames; ++i) {
      amount = alpha * (amountTarget - amount) + amountTarget;
      gain   = alpha * (gainTarget   - gain)   + gainTarget;
      Vec2d wet = gain * wetBuf[i];
      Vec2d dry = dryBuf[i];
      wetBuf[i] = amount * (wet - dry) + dry;
    }
    amount.store(mDsp->wetAmount);
    gain.store(mDsp->outputGain);
  } else if (!isBypassing) {
    auto& wetBuf = wetOutput.getBuffer2(0);
    Vec2d alpha = Vec2d(automationAlpha);
    Vec2d gain = Vec2d().load(mDsp->outputGain);
    Vec2d gainTarget = Vec2d().load(outputGainTarget);
    for (int i = 0; i < nFrames; ++i) {
      gain = alpha * (gainTarget - gain) + gainTarget;
      wetBuf[i] = gain * wetBuf[i];
    }
    gain.store(mDsp->outputGain);
  }

  if (isBypassing) {
    dryOutput.deinterleave(ioAudio, 2, nFrames);
  } else {
    wetOutput.deinterleave(ioAudio, 2, nFrames);
  }

  // Output-level RMS: 10 Hz one-pole over (M/S-domain when M/S) ioAudio.
  // Sampled here, after the wet/dry-mix + output-gain pass and after the
  // bypass deinterleave, so all signal-paths (compressed wet, bypass
  // dry-passthrough) feed the same meter; sampled BEFORE the M/S → L/R
  // restore so the per-channel readout matches the DSP domain the user
  // is operating in. Matches the time constant Overdraw uses.
  {
    constexpr double kVuMeterFrequency = 10.0;
    const double a = std::exp(-2.0 * M_PI * kVuMeterFrequency / sampleRate);
    for (int c = 0; c < 2; ++c) {
      double rms = mOutputLevelRms[c];
      for (int i = 0; i < nFrames; ++i) {
        const double s = ioAudio[c][i];
        rms = a * (rms - s * s) + s * s;
      }
      mOutputLevelRms[c] = rms;
    }
  }

  if (isMidSide) {
    MidSideToLeftRight(ioAudio, nFrames);
  }

  // VU meter snapshot to GUI-readable atomics + queues. The atomics are kept
  // for any direct-poll consumer (e.g. a custom paint timer); the senders
  // feed the IVMeterControls via OnIdle drain.
  //
  //   inputData  → "Input"  meter (audio-path RMS, post-input-gain)
  //   gainData   → "Gain"   meter (spline GR delta, dB-converted to amp)
  //   outputData → "Output" meter (audio-path RMS, post-output-gain)
  //   envData    → spline editor's "current input" dot (env-follower
  //                output, i.e. the X coord on the curve)
  ISenderData<2, float> inputData {kCtrlTagInputMeter,    2, 0};
  ISenderData<2, float> gainData  {kCtrlTagGainMeter,     2, 0};
  ISenderData<2, float> outputData{kCtrlTagOutputMeter,   2, 0};
  ISenderData<2, float> envData   {kCtrlTagSplineEditor,  2, 0};
  for (int c = 0; c < 2; ++c) {
    mLevelVuMeterResults[c].store(static_cast<float>(mDsp->levelVuMeterBuffer[c]));
    mGainVuMeterResults[c].store(static_cast<float>(mDsp->gainVuMeterBuffer[c]));
    // IVMeterControl::EResponse::Log wants linear amplitude.
    inputData.vals[c]  = static_cast<float>(std::sqrt(std::max(0.0, mInputLevelRms[c])));
    gainData.vals[c]   = static_cast<float>(std::pow(10.0, mDsp->gainVuMeterBuffer[c]  / 20.0));
    outputData.vals[c] = static_cast<float>(std::sqrt(std::max(0.0, mOutputLevelRms[c])));
    envData.vals[c]    = static_cast<float>(std::pow(10.0, mDsp->levelVuMeterBuffer[c] / 20.0));
  }
  mInputLevelSender.PushData(inputData);
  mGainMeterSender.PushData(gainData);
  mOutputLevelSender.PushData(outputData);
  mLevelMeterSender.PushData(envData);
}

#endif // IPLUG_DSP

// OnIdle is in the API base, not gated by IPLUG_DSP/IPLUG_EDITOR.
// TransmitData is safe to call whether or not the editor is currently open
// (it just no-ops via SendControlMsgFromDelegate when there's no UI).
void Curvessor::OnIdle()
{
  // Each sender targets exactly one control. The Input meter and the
  // spline editor's "current input" dot used to share data but no
  // longer do — they live in different domains (audio-path RMS vs.
  // curve-lookup envelope), so they need separate streams.
  mInputLevelSender.TransmitData(*this);
  mGainMeterSender.TransmitData(*this);
  mOutputLevelSender.TransmitData(*this);
  mLevelMeterSender.TransmitData(*this);

#if IPLUG_EDITOR
  // If the editor is closed, the cached side-panel knob / row-label
  // pointers below are dangling (their controls were destroyed in
  // IGraphicsNanoVG::OnViewDestroyed → RemoveAllControls). OnUIClose
  // nulls them, but we still need this guard for the timer firing
  // before OnUIClose has run (or in iPlug2 versions that don't call it).
  auto* ui = GetUI();
  if (!ui) return;

  // Force a periodic repaint of the spline editor so it picks up external
  // param changes — host automation, undo, edits via the knot-panel knobs,
  // anything that isn't a drag inside the spline editor itself. Cheap;
  // SetDirty just marks the control region invalid for the next frame.
  if (auto* ctrl = ui->GetControlWithTag(kCtrlTagSplineEditor)) {
    ctrl->SetDirty(false);
  }

  // Sync the side-panel knobs (X / Y / Tan / Smooth / Link) to their
  // currently-bound params' live values. The spline editor changes those
  // params via SendParameterValueFromUI directly (no mVals tying it to
  // them), so the framework's automatic UpdatePeers path doesn't reach
  // these knobs — without this pull, dragging a knot in the editor would
  // leave the panel knobs frozen on whatever they showed at the last
  // click. iplug_helpers::SyncControlFromParam does the
  // SetValueFromDelegate pull (and is null-safe).
  iplug_helpers::SyncControlFromParam(*this, mKnotPanelKnobX);
  iplug_helpers::SyncControlFromParam(*this, mKnotPanelKnobY);
  iplug_helpers::SyncControlFromParam(*this, mKnotPanelKnobTan);
  iplug_helpers::SyncControlFromParam(*this, mKnotPanelKnobSmoothness);
  iplug_helpers::SyncControlFromParam(*this, mKnotPanelLink);

  // Matrix row labels + meter per-track labels: "Left"/"Right" in stereo
  // mode, "Mid"/"Side" in M/S mode (single-letter for the narrow meter
  // bars). SetStr / SetTrackName are no-ops when the new string matches
  // the old, so it's cheap to call every frame. The meters need no
  // SetDirty here because the per-frame TransmitData already marks them
  // dirty.
  const bool ms = GetParam(kMidSide)->Bool();
  if (mRowLabelL && mRowLabelR) {
    mRowLabelL->SetStr(ms ? "Mid"  : "Left");
    mRowLabelR->SetStr(ms ? "Side" : "Right");
  }
  auto flipTrackLabels = [ms](iplug::igraphics::IVTrackControlBase* meter) {
    if (!meter) return;
    meter->SetTrackName(0, ms ? "M" : "L");
    meter->SetTrackName(1, ms ? "S" : "R");
  };
  flipTrackLabels(mInputMeter);
  flipTrackLabels(mGainMeter);
  flipTrackLabels(mOutputMeter);
#endif
}

void Curvessor::OnUIClose()
{
  // IGraphics destroys all controls when the editor closes (see
  // IGraphicsNanoVG::OnViewDestroyed -> RemoveAllControls). Our cached
  // side-panel pointers would then dangle; OnIdle would dereference them
  // on the next timer tick and crash. Null them here so the OnIdle guard
  // can detect "no editor" cleanly.
#if IPLUG_EDITOR
  mKnotPanelKnobX          = nullptr;
  mKnotPanelKnobY          = nullptr;
  mKnotPanelKnobTan        = nullptr;
  mKnotPanelKnobSmoothness = nullptr;
  mKnotPanelLink           = nullptr;
  mRowLabelL               = nullptr;
  mRowLabelR               = nullptr;
  mInputMeter              = nullptr;
  mGainMeter               = nullptr;
  mOutputMeter             = nullptr;
#endif
}

// =============================================================================
// Linkable-pair propagation. Called whenever any param changes, on the UI
// thread. For the 10 linkable pairs (Input-Gain / Output-Gain / Wet /
// Feedback / Attack / Release / Attack-Delay / Release-Delay / RMS-Time /
// HP-Cutoff), if the pair's _is_linked toggle is on and ch0 or ch1
// changed, copy the new value to the other channel so the two knobs track
// each other 1:1 while the user drags. The mPropagatingLinkChange guard
// breaks the recursive cycle (ch0 → ch1 → ch0 → …).
// =============================================================================
void Curvessor::OnParamChangeUI(int paramIdx, EParamSource source)
{
#if IPLUG_EDITOR
  static constexpr int kLinkableCh0Bases[] = {
    kInputGain_ch0,      kOutputGain_ch0,     kWet_ch0,
    kFeedbackAmount_ch0, kAttack_ch0,         kRelease_ch0,
    kAttackDelay_ch0,    kReleaseDelay_ch0,   kRMSTime_ch0,
    kHighPassCutoff_ch0,
  };
  iplug_helpers::PropagateLinkedParam(
    *this, paramIdx, source, kLinkableCh0Bases, mPropagatingLinkChange);
#endif
}

#if IPLUG_EDITOR
void Curvessor::SetSelectedKnot(int knotIdx, int channel)
{
  if (knotIdx < 0) return;
  if (knotIdx == mSelectedKnot && channel == mSelectedChannel) return;

  mSelectedKnot = knotIdx;
  mSelectedChannel = channel;

  const int base   = kKnot1_enabled + knotIdx * 10;
  const int chBase = base + 2 + channel * 4;

  // Rebind the side-panel controls to the newly-selected knot's params.
  // iplug_helpers::RebindControlToParam updates SetParamIdx and pulls
  // the new param's normalized value through immediately — without that
  // pull the knob would briefly display the previously-bound value
  // until the next host SetValueFromDelegate cycle.
  iplug_helpers::RebindControlToParam(*this, mKnotPanelKnobX,          chBase + 0);
  iplug_helpers::RebindControlToParam(*this, mKnotPanelKnobY,          chBase + 1);
  iplug_helpers::RebindControlToParam(*this, mKnotPanelKnobTan,        chBase + 2);
  iplug_helpers::RebindControlToParam(*this, mKnotPanelKnobSmoothness, chBase + 3);
  iplug_helpers::RebindControlToParam(*this, mKnotPanelLink,           base + 1);

  if (auto* ui = GetUI()) {
    ui->SetAllControlsDirty();
  }
}
#endif
