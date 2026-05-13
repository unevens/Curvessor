#include "Curvessor.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
#include "IControls.h"
#include "SplineEditorDsp.hpp"   // juicy::GuiSpline (JUCE-free scalar eval)
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

// =============================================================================
// Visual palette — "rack on a spacestation". Dark panel chassis, LCD-style
// spline editor with a low-contrast teal grid. The actual chassis bitmap is
// expected to be a future asset; these colours are picked to read well over
// either a flat dark fill or such a bitmap.
// =============================================================================

// Chassis (panel) — used when no background image is loaded yet.
static const IColor kPanelBg          (255,  14,  18,  24);
static const IColor kPanelFrame       (255,  44,  56,  68);

// LCD area (spline editor + meters).
static const IColor kLcdBg            (255,   6,  12,  16);
static const IColor kLcdFrame         (255,  60,  90, 105);
static const IColor kLcdGridMajor     (255,  44,  72,  88);
static const IColor kLcdGridMinor     (255,  22,  38,  48);
static const IColor kLcdAxisLabel     (255,  90, 140, 158);
static const IColor kLcdIdentityLine  (160,  60,  90, 105);

// Spline content.
static const IColor kLcdCurveCh0      (255,  80, 200, 230);  // bright cyan
static const IColor kLcdCurveCh1      (255, 240, 170,  90);  // warm amber
static const IColor kLcdKnotCh0       (255, 150, 220, 245);
static const IColor kLcdKnotCh1       (255, 255, 200, 130);
static const IColor kLcdKnotGhostCh0  (130,  80, 160, 200);
static const IColor kLcdKnotGhostCh1  (130, 200, 130,  80);
static const IColor kLcdKnotRing      (255,  10,  16,  22);
static const IColor kLcdSelectedHalo  (255, 255, 255, 255);
static const IColor kLcdTangentLine   (220, 220, 235, 200);
static const IColor kLcdTangentHandle (255, 250, 220, 100);
static const IColor kLcdLevelDotCh0   (255, 200, 240, 255);
static const IColor kLcdLevelDotCh1   (255, 255, 220, 180);
static const IColor kLcdGrLineCh0     (190, 130, 210, 240);
static const IColor kLcdGrLineCh1     (190, 245, 190, 130);

// Text styles.
static const IText kTitleText(20, IColor(255, 200, 220, 230),
                              nullptr, EAlign::Center);
static const IText kVersionText(10, IColor(255, 110, 140, 155),
                                nullptr, EAlign::Far);
static const IText kLcdAxisLabelText(10, kLcdAxisLabel, nullptr, EAlign::Center);
static const IText kLcdAxisLabelTextLeft(10, kLcdAxisLabel, nullptr, EAlign::Near);

// Shared IVStyle for every Curvessor panel control — knobs, switches,
// tabs, meters. Dark base, cyan accents, slim 1px frames.
static const IVStyle kCurvessorStyle = DEFAULT_STYLE
  .WithColor(kBG, IColor(255,  20,  26,  32))
  .WithColor(kFG, IColor(255,  60, 110, 130))
  .WithColor(kPR, IColor(255,  80, 200, 230))
  .WithColor(kFR, IColor(255,  60,  85, 100))
  .WithColor(kHL, IColor(255, 120, 180, 200))
  .WithColor(kSH, IColor(255,   4,   8,  12))
  .WithColor(kX1, IColor(255, 240, 170,  90))
  .WithFrameThickness(1.f)
  .WithLabelText(IText(11, IColor(255, 170, 200, 215),
                       nullptr, EAlign::Center))
  .WithValueText(IText(10, IColor(255, 190, 215, 225),
                       nullptr, EAlign::Center));

// =============================================================================
// SplineControl — IGraphics control that draws Curvessor's gain curve and
// lets the user drag the editable knots. Multi-channel: when a knot is
// unlinked, both ch0 and ch1 positions are drawn and individually
// draggable. Selecting a knot (mouse-down) rebinds the side-panel knobs
// for X / Y / Tangent / Link to that knot via Curvessor::SetSelectedKnot.
// =============================================================================
class CurvessorSplineControl final : public IControl
{
public:
  CurvessorSplineControl(const IRECT& bounds)
  : IControl(bounds)
  {
  }

  void Draw(IGraphics& g) override
  {
    auto* del = GetDelegate();

    // LCD chassis.
    g.FillRect(kLcdBg, mRECT);

    DrawDbGrid(g);

    // Refresh scalar spline from the live params and count active knots.
    const int numActive = RefreshSplineFromParams();

    // Detect whether any knot is unlinked — if all are linked, ch0 and ch1
    // curves overlap so we draw a single curve.
    bool anyUnlinked = false;
    for (int i = 0; i < kNumKnots; ++i) {
      const int base = kKnot1_enabled + i * 10;
      if (!del->GetParam(base + 0)->Bool()) continue;
      if (!del->GetParam(base + 1)->Bool()) { anyUnlinked = true; break; }
    }

    // Curves — ch0 first, then ch1 on top if any knot is split.
    DrawCurve(g, 0, kLcdCurveCh0, numActive);
    if (anyUnlinked) DrawCurve(g, 1, kLcdCurveCh1, numActive);

    // Knots.
    auto* plug = static_cast<Curvessor*>(del);
    const int selKnot = plug->mSelectedKnot;
    const int selCh   = plug->mSelectedChannel;

    for (int i = 0; i < kNumKnots; ++i) {
      const int base = kKnot1_enabled + i * 10;
      const bool enabled = del->GetParam(base + 0)->Bool();
      const bool linked  = del->GetParam(base + 1)->Bool();
      for (int c = 0; c < 2; ++c) {
        if (linked && c > 0) break;
        const int chBase = base + 2 + c * 4;
        const float kx = DbToScreenX(del->GetParam(chBase + 0)->Value());
        const float ky = DbToScreenY(del->GetParam(chBase + 1)->Value());

        const bool hot = enabled
                      && ((i == mDraggedKnot && c == mDraggedChannel)
                       || (i == mHoverKnot   && c == mHoverChannel));
        const bool sel = enabled && (i == selKnot && c == selCh);

        if (enabled) {
          const IColor& fill = hot ? kLcdSelectedHalo
                                   : (c == 0 ? kLcdKnotCh0 : kLcdKnotCh1);
          const float r = sel ? kKnotRadius + 2.f : kKnotRadius;
          g.FillCircle(fill, kx, ky, r);
          g.DrawCircle(kLcdKnotRing, kx, ky, r, nullptr, sel ? 2.f : 1.f);
        } else {
          // Ghost: smaller, translucent. Still hit-testable for double-
          // click re-enable via FindKnotAt(includeDisabled=true).
          const IColor& fill = (c == 0) ? kLcdKnotGhostCh0 : kLcdKnotGhostCh1;
          g.FillCircle(fill, kx, ky, kKnotRadius - 1.f);
        }
      }
    }

    // Tangent handle on the currently-selected knot.
    if (selKnot >= 0) {
      const int selBase = kKnot1_enabled + selKnot * 10;
      if (del->GetParam(selBase + 0)->Bool()) {
        const bool linked = del->GetParam(selBase + 1)->Bool();
        DrawTangentHandle(g, selKnot, linked ? 0 : selCh);
      }
    }

    // Live "current input" dot per channel + GR vertical line to identity.
    for (int c = 0; c < 2; ++c) {
      const float amp = mCurrentLevelAmp[c];
      if (amp <= 0.f) continue;
      const double dB = 20.0 * std::log10(static_cast<double>(amp));
      if (dB < kKnotMin || dB > kKnotMax) continue;
      const double yDb = mSpline.process(dB, c, numActive);
      const float dx = DbToScreenX(dB);
      const float dy = DbToScreenY(yDb);
      const float identityY = DbToScreenY(dB);

      const IColor grCol  = (c == 0) ? kLcdGrLineCh0  : kLcdGrLineCh1;
      const IColor dotCol = (c == 0) ? kLcdLevelDotCh0 : kLcdLevelDotCh1;
      g.DrawLine(grCol, dx, dy, dx, identityY, nullptr, 2.f);
      g.FillCircle(dotCol, dx, dy, 4.f);
      g.DrawCircle(kLcdKnotRing, dx, dy, 4.f, nullptr, 1.f);
    }

    // LCD frame.
    g.DrawRect(kLcdFrame, mRECT, nullptr, 1.f);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    auto* del = GetDelegate();
    const bool preferCh1 = mod.R || mod.A;

    // Tangent handle on the currently-selected knot takes priority — it
    // overlaps the knot area visually but the hit is the smaller dot.
    const KnotHit thit = FindTangentAt(x, y);
    if (thit.knot >= 0) {
      mDraggedKnot = thit.knot;
      mDraggedChannel = thit.channel;
      mDraggingTangent = true;
      const int base = kKnot1_enabled + thit.knot * 10;
      const int chBase = base + 2 + thit.channel * 4;
      del->BeginInformHostOfParamChangeFromUI(chBase + 2);  // Tan_chC
      SetDirty(false);
      return;
    }

    // Knot body hit. Right-button / Alt biases the hit toward ch1 when both
    // channels of an unlinked knot are stacked on screen.
    const KnotHit hit = FindKnotAt(x, y, preferCh1, /*includeDisabled=*/false);
    mDraggedKnot = hit.knot;
    mDraggedChannel = hit.channel;
    mDraggingTangent = false;
    if (hit.knot >= 0) {
      const int base = kKnot1_enabled + hit.knot * 10;
      const int chBase = base + 2 + hit.channel * 4;
      del->BeginInformHostOfParamChangeFromUI(chBase + 0);  // X_chC
      del->BeginInformHostOfParamChangeFromUI(chBase + 1);  // Y_chC
      // Tell the plugin to rebind the side-panel knobs to this knot/channel.
      static_cast<Curvessor*>(del)->SetSelectedKnot(hit.knot, hit.channel);
    }
    SetDirty(false);
  }

  // Double-click toggles a knot's state. Matches the JUCE editor:
  //   - left / no-mod double-click on a knot → toggle `enabled` (add/remove)
  //   - right / alt double-click on a knot   → toggle `linked` (split L/R)
  // Disabled knots are hit-testable (drawn faintly as ghosts) so users can
  // re-enable them by double-clicking.
  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    const bool preferCh1 = mod.R || mod.A;
    const KnotHit hit = FindKnotAt(x, y, preferCh1, /*includeDisabled=*/true);
    if (hit.knot < 0) {
      SetDirty(false);
      return;
    }
    const int base = kKnot1_enabled + hit.knot * 10;
    auto* del = GetDelegate();
    if (preferCh1) {
      const bool linked = del->GetParam(base + 1)->Bool();
      SetParamBoolFromUI(base + 1, !linked);
    } else {
      const bool enabled = del->GetParam(base + 0)->Bool();
      SetParamBoolFromUI(base + 0, !enabled);
    }
    // Make this knot the selection so the side panel binds to it.
    static_cast<Curvessor*>(del)->SetSelectedKnot(hit.knot, hit.channel);
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override
  {
    if (mDraggedKnot < 0) return;
    const int base = kKnot1_enabled + mDraggedKnot * 10;
    const int chBase = base + 2 + mDraggedChannel * 4;
    auto* del = GetDelegate();

    if (mDraggingTangent) {
      // Tangent t = dy_dB / dx_dB, with the cursor offset measured from the
      // knot in screen space (y inverted). When the cursor is dragged near
      // the vertical through the knot, slope diverges → clamp.
      const float kx = DbToScreenX(del->GetParam(chBase + 0)->Value());
      const float ky = DbToScreenY(del->GetParam(chBase + 1)->Value());
      const float dxScreen = x - kx;
      const float dyScreen = ky - y;  // up on screen = positive dB

      const int tanIdx = chBase + 2;
      const IParam* tanParam = del->GetParam(tanIdx);
      double tNew;
      if (std::abs(dxScreen) < 1.f) {
        tNew = (dyScreen >= 0.f) ? tanParam->GetMax() : tanParam->GetMin();
      } else {
        tNew = static_cast<double>(dyScreen) / dxScreen;
        // If the user drags past the vertical, sign flips weirdly — clamp.
        tNew = std::clamp(tNew, tanParam->GetMin(), tanParam->GetMax());
      }
      del->SendParameterValueFromUI(tanIdx, tanParam->ToNormalized(tNew));
    }
    else {
      const int xIdx = chBase + 0;
      const int yIdx = chBase + 1;
      const IParam* xParam = del->GetParam(xIdx);
      const IParam* yParam = del->GetParam(yIdx);
      const double xDb = std::clamp(ScreenXToDb(x), xParam->GetMin(), xParam->GetMax());
      const double yDb = std::clamp(ScreenYToDb(y), yParam->GetMin(), yParam->GetMax());
      del->SendParameterValueFromUI(xIdx, xParam->ToNormalized(xDb));
      del->SendParameterValueFromUI(yIdx, yParam->ToNormalized(yDb));
    }

    SetDirty(false);
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    if (mDraggedKnot >= 0) {
      const int base = kKnot1_enabled + mDraggedKnot * 10;
      const int chBase = base + 2 + mDraggedChannel * 4;
      auto* del = GetDelegate();
      if (mDraggingTangent) {
        del->EndInformHostOfParamChangeFromUI(chBase + 2);
      } else {
        del->EndInformHostOfParamChangeFromUI(chBase + 0);
        del->EndInformHostOfParamChangeFromUI(chBase + 1);
      }
    }
    mDraggedKnot = -1;
    mDraggedChannel = 0;
    mDraggingTangent = false;
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod&) override
  {
    const KnotHit hit = FindKnotAt(x, y);
    if (hit.knot != mHoverKnot || hit.channel != mHoverChannel) {
      mHoverKnot = hit.knot;
      mHoverChannel = hit.channel;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    if (mHoverKnot != -1) {
      mHoverKnot = -1;
      mHoverChannel = 0;
      SetDirty(false);
    }
  }

  // Receives ISenderData<2, float> packets from Curvessor::OnIdle. The
  // payload carries the envelope-follower output per channel as a linear
  // amplitude (the meter widget needs amp for its Log response); we stash
  // it and read it back in Draw to position the live level dot on the
  // curve.
  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    if (msgTag != ISender<>::kUpdateMessage) return;
    IByteStream stream(pData, dataSize);
    int pos = 0;
    ISenderData<2, float> d;
    pos = stream.Get(&d, pos);
    mCurrentLevelAmp[0] = d.vals[0];
    mCurrentLevelAmp[1] = d.vals[1];
    // No SetDirty here — OnIdle already repaints us every frame.
  }

private:
  static constexpr int kNumKnots = 8;
  static constexpr double kKnotMin = -96.0;
  static constexpr double kKnotMax = 6.0;
  static constexpr float kKnotRadius = 6.f;
  static constexpr float kKnotHitRadiusSq = 20.f * 20.f;
  static constexpr float kTangentHandleOffset = 32.f;   // pixels from knot center
  static constexpr float kTangentHandleRadius = 4.f;
  static constexpr float kTangentHitRadiusSq = 14.f * 14.f;

  struct KnotHit { int knot; int channel; };

  juicy::GuiSpline mSpline{ curvessor::maxNumKnots };
  int mDraggedKnot = -1;
  int mDraggedChannel = 0;
  bool mDraggingTangent = false;
  int mHoverKnot = -1;
  int mHoverChannel = 0;
  // Live envelope-follower output per channel (linear amplitude). Drawn as
  // a dot riding along the curve to show where compression is acting now.
  std::array<float, 2> mCurrentLevelAmp{};

  float DbToScreenX(double db) const
  {
    return mRECT.L +
      static_cast<float>((db - kKnotMin) / (kKnotMax - kKnotMin)) * mRECT.W();
  }
  float DbToScreenY(double db) const
  {
    return mRECT.B -
      static_cast<float>((db - kKnotMin) / (kKnotMax - kKnotMin)) * mRECT.H();
  }
  double ScreenXToDb(float x) const
  {
    return kKnotMin +
      (x - mRECT.L) / static_cast<double>(mRECT.W()) * (kKnotMax - kKnotMin);
  }
  double ScreenYToDb(float y) const
  {
    return kKnotMin +
      (mRECT.B - y) / static_cast<double>(mRECT.H()) * (kKnotMax - kKnotMin);
  }

  void DrawCurve(IGraphics& g, int channel, const IColor& col, int numActive)
  {
    float prevX = 0.f, prevY = 0.f;
    constexpr int kNumSamples = 240;
    for (int i = 0; i <= kNumSamples; ++i) {
      const double xDb = kKnotMin + (kKnotMax - kKnotMin) * (i / double(kNumSamples));
      const double yDb = mSpline.process(xDb, channel, numActive);
      const float sx = DbToScreenX(xDb);
      const float sy = DbToScreenY(yDb);
      if (i > 0) g.DrawLine(col, prevX, prevY, sx, sy, nullptr, 2.f);
      prevX = sx;
      prevY = sy;
    }
  }

  // LCD-style dB grid with major lines at 24 dB intervals and minor lines
  // halfway between. Labels along the bottom (X) and the left (Y) edges in
  // a low-contrast teal so they don't compete with the curves. The y = x
  // identity diagonal is drawn over the grid as a faint reference.
  void DrawDbGrid(IGraphics& g)
  {
    static constexpr int kMajor[] = { -96, -72, -48, -24, 0 };
    static constexpr int kMinor[] = { -84, -60, -36, -12 };

    // Minor lines first so the major lines paint on top.
    for (int db : kMinor) {
      const float xs = DbToScreenX(db);
      const float ys = DbToScreenY(db);
      g.DrawLine(kLcdGridMinor, xs, mRECT.T + 1, xs, mRECT.B - 1, nullptr, 1.f);
      g.DrawLine(kLcdGridMinor, mRECT.L + 1, ys, mRECT.R - 1, ys, nullptr, 1.f);
    }

    for (int db : kMajor) {
      const float xs = DbToScreenX(db);
      const float ys = DbToScreenY(db);
      g.DrawLine(kLcdGridMajor, xs, mRECT.T + 1, xs, mRECT.B - 1, nullptr, 1.f);
      g.DrawLine(kLcdGridMajor, mRECT.L + 1, ys, mRECT.R - 1, ys, nullptr, 1.f);

      char buf[8];
      std::snprintf(buf, sizeof(buf), "%d", db);

      // X-axis label, bottom edge, slightly inset.
      const IRECT xLabel(xs - 18.f, mRECT.B - 15.f, xs + 18.f, mRECT.B - 3.f);
      g.DrawText(kLcdAxisLabelText, buf, xLabel);

      // Y-axis label, left edge. Skip the bottom-left corner where the two
      // axes overlap visually (-96 vs -96 reads as a single label).
      if (db != -96) {
        const IRECT yLabel(mRECT.L + 3.f, ys - 7.f, mRECT.L + 32.f, ys + 7.f);
        g.DrawText(kLcdAxisLabelTextLeft, buf, yLabel);
      }
    }

    // y = x identity diagonal — a "no-change" reference for the user when
    // reading the curve. Faint so the actual curve is clearly the figure.
    const float topRightX = DbToScreenX(kKnotMax);
    const float topRightY = DbToScreenY(kKnotMax);
    const float bottomLeftX = DbToScreenX(kKnotMin);
    const float bottomLeftY = DbToScreenY(kKnotMin);
    g.DrawLine(kLcdIdentityLine,
               bottomLeftX, bottomLeftY,
               topRightX,   topRightY,
               nullptr, 1.f);
  }

  // Small param-write helpers — wrap Begin/Send/End for a single-shot write.
  void SetParamFromUI(int paramIdx, double clampedValue)
  {
    auto* del = GetDelegate();
    const IParam* p = del->GetParam(paramIdx);
    del->BeginInformHostOfParamChangeFromUI(paramIdx);
    del->SendParameterValueFromUI(paramIdx, p->ToNormalized(clampedValue));
    del->EndInformHostOfParamChangeFromUI(paramIdx);
  }
  void SetParamBoolFromUI(int paramIdx, bool value)
  {
    SetParamFromUI(paramIdx, value ? 1.0 : 0.0);
  }

  // Hit-test knots, returning the nearest hit subject to channel-preference
  // and enabled-state filters. Mirrors juicy/SplineEditor::selectKnot — the
  // RMB / Alt modifier biases toward ch1 when both channels of an unlinked
  // knot sit at the same screen position. For double-click we also include
  // disabled knots so the user can target their (ghost-drawn) positions.
  KnotHit FindKnotAt(float x, float y,
                     bool preferCh1 = false,
                     bool includeDisabled = false)
  {
    auto* del = GetDelegate();
    KnotHit nearest[2] = {{-1, 0}, {-1, 1}};   // best ch0 hit, best ch1 hit
    float nearestDist2[2] = {kKnotHitRadiusSq, kKnotHitRadiusSq};

    for (int i = 0; i < kNumKnots; ++i) {
      const int base = kKnot1_enabled + i * 10;
      const bool enabled = del->GetParam(base + 0)->Bool();
      if (!includeDisabled && !enabled) continue;
      const bool linked = del->GetParam(base + 1)->Bool();
      for (int c = 0; c < 2; ++c) {
        if (linked && c > 0) break;
        const int chBase = base + 2 + c * 4;
        const float kx = DbToScreenX(del->GetParam(chBase + 0)->Value());
        const float ky = DbToScreenY(del->GetParam(chBase + 1)->Value());
        const float dx = kx - x;
        const float dy = ky - y;
        const float d2 = dx * dx + dy * dy;
        // Linked knot contributes to the ch0 hit slot; unlinked ch1 to ch1.
        const int hitCh = linked ? 0 : c;
        if (d2 < nearestDist2[hitCh]) {
          nearestDist2[hitCh] = d2;
          nearest[hitCh] = {i, hitCh};
        }
      }
    }

    // Pick channel preference; fall back to the other if the preferred has
    // no hit.
    int pick;
    if (preferCh1 && nearest[1].knot >= 0) {
      pick = 1;
    } else if (nearest[0].knot >= 0 && nearest[1].knot >= 0) {
      pick = (nearestDist2[0] <= nearestDist2[1]) ? 0 : 1;
    } else {
      pick = (nearest[0].knot >= 0) ? 0 : 1;
    }
    return nearest[pick];
  }

  // Tangent handle hit-test, on the currently-selected knot/channel only.
  // We don't show handles on every visible knot — too much visual clutter
  // for the small editor — so the user picks a knot first, then drags its
  // tangent.
  KnotHit FindTangentAt(float x, float y)
  {
    auto* plug = static_cast<Curvessor*>(GetDelegate());
    const int knotIdx = plug->mSelectedKnot;
    const int channel = plug->mSelectedChannel;
    if (knotIdx < 0) return {-1, 0};

    auto* del = GetDelegate();
    const int base = kKnot1_enabled + knotIdx * 10;
    if (!del->GetParam(base + 0)->Bool()) return {-1, 0};
    // When linked, the side panel's "ch1" selection is meaningless; pin to ch0.
    const bool linked = del->GetParam(base + 1)->Bool();
    const int effectiveCh = linked ? 0 : channel;

    float hx, hy;
    TangentHandleScreenPos(knotIdx, effectiveCh, hx, hy);
    const float dx = hx - x;
    const float dy = hy - y;
    if (dx * dx + dy * dy < kTangentHitRadiusSq) {
      return {knotIdx, effectiveCh};
    }
    return {-1, 0};
  }

  // Compute the (x, y) screen position of the tangent handle for the given
  // knot/channel. Uses atan(t) to keep the handle on a fixed-radius circle,
  // so steep tangents don't push the handle off-screen.
  void TangentHandleScreenPos(int knotIdx, int channel, float& outX, float& outY)
  {
    auto* del = GetDelegate();
    const int base = kKnot1_enabled + knotIdx * 10;
    const int chBase = base + 2 + channel * 4;
    const float kx = DbToScreenX(del->GetParam(chBase + 0)->Value());
    const float ky = DbToScreenY(del->GetParam(chBase + 1)->Value());
    const double t = del->GetParam(chBase + 2)->Value();
    const double alpha = std::atan(t);
    outX = kx + kTangentHandleOffset * static_cast<float>(std::cos(alpha));
    outY = ky - kTangentHandleOffset * static_cast<float>(std::sin(alpha));
  }

  void DrawTangentHandle(IGraphics& g, int knotIdx, int channel)
  {
    auto* del = GetDelegate();
    const int base = kKnot1_enabled + knotIdx * 10;
    if (!del->GetParam(base + 0)->Bool()) return;
    const int chBase = base + 2 + channel * 4;
    const float kx = DbToScreenX(del->GetParam(chBase + 0)->Value());
    const float ky = DbToScreenY(del->GetParam(chBase + 1)->Value());

    float hx, hy;
    TangentHandleScreenPos(knotIdx, channel, hx, hy);

    g.DrawLine(kLcdTangentLine, kx, ky, hx, hy, nullptr, 1.f);

    const bool isDragging = (knotIdx == mDraggedKnot
                          && channel == mDraggedChannel
                          && mDraggingTangent);
    const IColor handleCol = isDragging ? kLcdSelectedHalo : kLcdTangentHandle;
    g.FillCircle(handleCol, hx, hy, kTangentHandleRadius);
    g.DrawCircle(kLcdKnotRing, hx, hy, kTangentHandleRadius, nullptr, 1.f);
  }

  // Populates mSpline from the live param values. Returns count of active
  // knots (fixed anchor + each enabled editable knot). Mirrors the
  // ProcessBlock-side UpdateSplineFromParams helper, but per-channel.
  int RefreshSplineFromParams()
  {
    auto* del = GetDelegate();
    int n = 0;
    for (int c = 0; c < 2; ++c) {
      mSpline.knot(n).x[c] = -96.0;
      mSpline.knot(n).y[c] = -96.0;
      mSpline.knot(n).t[c] = 1.0;
      mSpline.knot(n).s[c] = 1.0;
    }
    ++n;
    for (int i = 0; i < kNumKnots; ++i) {
      const int base = kKnot1_enabled + i * 10;
      if (!del->GetParam(base + 0)->Bool()) continue;
      const bool linked = del->GetParam(base + 1)->Bool();
      for (int c = 0; c < 2; ++c) {
        const int chSrc = linked ? 0 : c;
        const int chBase = base + 2 + chSrc * 4;
        mSpline.knot(n).x[c] = del->GetParam(chBase + 0)->Value();
        mSpline.knot(n).y[c] = del->GetParam(chBase + 1)->Value();
        mSpline.knot(n).t[c] = del->GetParam(chBase + 2)->Value();
        mSpline.knot(n).s[c] = del->GetParam(chBase + 3)->Value();
      }
      ++n;
    }
    return n;
  }
};

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
    // Layout for 1024×640: title strip on top, spline editor + knot panel
    // band, 2-row param grid, slim meters at the bottom.
    const IRECT bounds = pGraphics->GetBounds();
    const IRECT innerBounds = bounds.GetPadded(-10.f);

    // Top strip: title centred, version in the top-right corner.
    const IRECT topStrip = innerBounds.GetFromTop(28);
    const IRECT titleBounds   = topStrip.GetCentredInside(360, 26);
    const IRECT versionBounds = topStrip.GetFromRight(240);

    // Spline + knot-panel band, 312 px tall starting just below the title.
    const IRECT splinePanelArea =
      innerBounds.GetReducedFromTop(36).GetFromTop(312);
    const IRECT splineEditorRect = splinePanelArea.GetReducedFromRight(208);
    const IRECT knotPanelRect    = splinePanelArea.GetFromRight(200);

    // Knot panel: 2×2 knob grid (X/Y top row, Tan/Smooth bottom row) + a
    // Link L/R toggle along the bottom.
    const IRECT knotPanelKnobsArea = knotPanelRect.GetReducedFromBottom(44);
    const IRECT knotPanelLinkArea  = knotPanelRect.GetFromBottom(44);
    auto knobCell = [&](int col, int row) {
      const float w = knotPanelKnobsArea.W() / 2.f;
      const float h = knotPanelKnobsArea.H() / 2.f;
      return IRECT(knotPanelKnobsArea.L + col * w,
                   knotPanelKnobsArea.T + row * h,
                   knotPanelKnobsArea.L + (col + 1) * w,
                   knotPanelKnobsArea.T + (row + 1) * h).GetPadded(-4);
    };
    const IRECT knobXRect      = knobCell(0, 0);
    const IRECT knobYRect      = knobCell(1, 0);
    const IRECT knobTanRect    = knobCell(0, 1);
    const IRECT knobSmoothRect = knobCell(1, 1);
    const IRECT linkRect       = knotPanelLinkArea.GetCentredInside(140, 28);

    // 2-row param grid below the spline band, 9 cells per row.
    const IRECT gridArea =
      innerBounds.GetReducedFromTop(354).GetReducedFromBottom(38);
    constexpr int kGridCols = 9;
    constexpr int kGridRows = 2;
    auto cellAt = [&](int col, int row) {
      const float colW = gridArea.W() / kGridCols;
      const float rowH = gridArea.H() / kGridRows;
      return IRECT(gridArea.L + col * colW, gridArea.T + row * rowH,
                   gridArea.L + (col + 1) * colW, gridArea.T + (row + 1) * rowH).GetPadded(-4);
    };

    // Meters in a slim strip along the bottom edge.
    const IRECT metersArea     = innerBounds.GetFromBottom(32);
    const IRECT levelMeterRect = metersArea.GetFromLeft(metersArea.W() * 0.5f).GetPadded(-4, 0, -4, 0);
    const IRECT gainMeterRect  = metersArea.GetFromRight(metersArea.W() * 0.5f).GetPadded(-4, 0, -4, 0);

    if (pGraphics->NControls()) {
      pGraphics->GetBackgroundControl()->SetTargetAndDrawRECTs(bounds);
      pGraphics->GetControlWithTag(kCtrlTagTitle)->SetTargetAndDrawRECTs(titleBounds);
      pGraphics->GetControlWithTag(kCtrlTagVersionNumber)->SetTargetAndDrawRECTs(versionBounds);
      pGraphics->GetControlWithTag(kCtrlTagSplineEditor)->SetTargetAndDrawRECTs(splineEditorRect);
      pGraphics->GetControlWithTag(kCtrlTagLevelMeter)->SetTargetAndDrawRECTs(levelMeterRect);
      pGraphics->GetControlWithTag(kCtrlTagGainMeter)->SetTargetAndDrawRECTs(gainMeterRect);
      if (mKnotPanelKnobX)          mKnotPanelKnobX         ->SetTargetAndDrawRECTs(knobXRect);
      if (mKnotPanelKnobY)          mKnotPanelKnobY         ->SetTargetAndDrawRECTs(knobYRect);
      if (mKnotPanelKnobTan)        mKnotPanelKnobTan       ->SetTargetAndDrawRECTs(knobTanRect);
      if (mKnotPanelKnobSmoothness) mKnotPanelKnobSmoothness->SetTargetAndDrawRECTs(knobSmoothRect);
      if (mKnotPanelLink)           mKnotPanelLink          ->SetTargetAndDrawRECTs(linkRect);
      return;
    }

    pGraphics->SetLayoutOnResize(true);
    pGraphics->AttachCornerResizer(EUIResizerMode::Size, true);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    // Dark chassis. A textured background bitmap (rack-on-spacestation art)
    // can replace this later via AttachBackground; the rest of the palette
    // is designed to read over either.
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

    // Knot side-panel.
    {
      const int base = kKnot1_enabled + mSelectedKnot * 10;
      const int chBase = base + 2 + mSelectedChannel * 4;
      mKnotPanelKnobX          = pGraphics->AttachControl(new IVKnobControl(knobXRect,      chBase + 0, "X (dB)",   kCurvessorStyle));
      mKnotPanelKnobY          = pGraphics->AttachControl(new IVKnobControl(knobYRect,      chBase + 1, "Y (dB)",   kCurvessorStyle));
      mKnotPanelKnobTan        = pGraphics->AttachControl(new IVKnobControl(knobTanRect,    chBase + 2, "Tangent",  kCurvessorStyle));
      mKnotPanelKnobSmoothness = pGraphics->AttachControl(new IVKnobControl(knobSmoothRect, chBase + 3, "Smooth",   kCurvessorStyle));
      mKnotPanelLink           = pGraphics->AttachControl(new IVSwitchControl(linkRect, base + 1, "Link L/R", kCurvessorStyle));
    }

    // Linkable pair: knob on ch0 + small "L" toggle bound to _is_linked.
    auto attachLinkable = [&](const IRECT& cell, int ch0Idx, const char* label) {
      const IRECT knobRect = cell.GetReducedFromRight(20);
      const IRECT lRect   = cell.GetFromRight(18).GetPadded(0, -6, 0, -6);
      pGraphics->AttachControl(new IVKnobControl(knobRect, ch0Idx, label, kCurvessorStyle));
      pGraphics->AttachControl(new IVSwitchControl(lRect, ch0Idx + 2, "L", kCurvessorStyle));
    };

    // Row 0 — globals + filter.
    pGraphics->AttachControl(new IVSwitchControl(cellAt(0, 0), kMidSide,                "Mid/Side",   kCurvessorStyle));
    pGraphics->AttachControl(new IVSwitchControl(cellAt(1, 0), kSideChain,              "Sidechain",  kCurvessorStyle));
    pGraphics->AttachControl(new IVTabSwitchControl(cellAt(2, 0), kOversampling,
                              {"1x", "2x", "4x", "8x", "16x", "32x"}, "Oversampling", kCurvessorStyle));
    pGraphics->AttachControl(new IVSwitchControl(cellAt(3, 0), kLinearPhaseOversampling, "Lin Phase", kCurvessorStyle));
    attachLinkable(cellAt(4, 0), kHighPassCutoff_ch0, "HP Cutoff");
    pGraphics->AttachControl(new IVTabSwitchControl(cellAt(5, 0), kHighPassOrder,
                              {"Off", "6", "12", "18"}, "HP Order", kCurvessorStyle));
    pGraphics->AttachControl(new IVKnobControl(cellAt(6, 0), kStereoLink,    "Stereo Link", kCurvessorStyle));
    pGraphics->AttachControl(new IVKnobControl(cellAt(7, 0), kSmoothingTime, "Smoothing",   kCurvessorStyle));
    // cellAt(8, 0) intentionally empty for now.

    // Row 1 — signal-flow params (input → output, plus envelope shape).
    attachLinkable(cellAt(0, 1), kInputGain_ch0,      "In Gain");
    attachLinkable(cellAt(1, 1), kOutputGain_ch0,     "Out Gain");
    attachLinkable(cellAt(2, 1), kWet_ch0,            "Wet");
    attachLinkable(cellAt(3, 1), kFeedbackAmount_ch0, "Feedback");
    attachLinkable(cellAt(4, 1), kAttack_ch0,         "Attack");
    attachLinkable(cellAt(5, 1), kRelease_ch0,        "Release");
    attachLinkable(cellAt(6, 1), kAttackDelay_ch0,    "A Delay");
    attachLinkable(cellAt(7, 1), kReleaseDelay_ch0,   "R Delay");
    attachLinkable(cellAt(8, 1), kRMSTime_ch0,        "RMS Time");

    // VU meters fed by ISender packets pushed from ProcessBlock.
    const IVStyle meterStyle = kCurvessorStyle.WithDrawFrame(false);
    pGraphics->AttachControl(
      new IVMeterControl<2>(levelMeterRect, "Level", meterStyle,
                            EDirection::Horizontal, {"L", "R"}, 0,
                            IVMeterControl<2>::EResponse::Log, -60.f, 6.f),
      kCtrlTagLevelMeter);
    pGraphics->AttachControl(
      new IVMeterControl<2>(gainMeterRect, "Gain", meterStyle,
                            EDirection::Horizontal, {"L", "R"}, 0,
                            IVMeterControl<2>::EResponse::Log, -60.f, 6.f),
      kCtrlTagGainMeter);
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

  if (isMidSide) {
    MidSideToLeftRight(ioAudio, nFrames);
  }

  // VU meter snapshot to GUI-readable atomics + queues. The atomics are kept
  // for any direct-poll consumer (e.g. a custom paint timer); the senders
  // feed the IVMeterControls via OnIdle drain.
  ISenderData<2, float> levelData{kCtrlTagLevelMeter, 2, 0};
  ISenderData<2, float> gainData{kCtrlTagGainMeter, 2, 0};
  for (int c = 0; c < 2; ++c) {
    mLevelVuMeterResults[c].store(static_cast<float>(mDsp->levelVuMeterBuffer[c]));
    mGainVuMeterResults[c].store(static_cast<float>(mDsp->gainVuMeterBuffer[c]));
    // IVMeterControl::EResponse::Log wants linear amplitude.
    levelData.vals[c] = static_cast<float>(std::pow(10.0, mDsp->levelVuMeterBuffer[c] / 20.0));
    gainData.vals[c]  = static_cast<float>(std::pow(10.0, mDsp->gainVuMeterBuffer[c]  / 20.0));
  }
  mLevelMeterSender.PushData(levelData);
  mGainMeterSender.PushData(gainData);
}

#endif // IPLUG_DSP

// OnIdle is in the API base, not gated by IPLUG_DSP/IPLUG_EDITOR.
// TransmitData is safe to call whether or not the editor is currently open
// (it just no-ops via SendControlMsgFromDelegate when there's no UI).
void Curvessor::OnIdle()
{
  // Level meter packets go to both the meter widget and the spline editor —
  // the editor uses them to plot a moving "current input" dot on the curve.
  // TransmitDataToControlsWithTags overrides d.ctrlTag per recipient so the
  // same queue payload reaches both controls.
  mLevelMeterSender.TransmitDataToControlsWithTags(
    *this, {kCtrlTagLevelMeter, kCtrlTagSplineEditor});
  mGainMeterSender.TransmitData(*this);

#if IPLUG_EDITOR
  // Force a periodic repaint of the spline editor so it picks up external
  // param changes — host automation, undo, edits via the knot-panel knobs,
  // anything that isn't a drag inside the spline editor itself. Cheap;
  // SetDirty just marks the control region invalid for the next frame.
  if (auto* ui = GetUI()) {
    if (auto* ctrl = ui->GetControlWithTag(kCtrlTagSplineEditor)) {
      ctrl->SetDirty(false);
    }
  }
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

  // Rebind the side-panel controls to the freshly-selected knot's params.
  // SetParamIdx swaps the index in mVals and calls SetDirty(false); iPlug2
  // pulls the new value into the control's internal state on the next
  // SetValueFromDelegate cycle, so the knob shows the correct reading.
  if (mKnotPanelKnobX)          mKnotPanelKnobX         ->SetParamIdx(chBase + 0);
  if (mKnotPanelKnobY)          mKnotPanelKnobY         ->SetParamIdx(chBase + 1);
  if (mKnotPanelKnobTan)        mKnotPanelKnobTan       ->SetParamIdx(chBase + 2);
  if (mKnotPanelKnobSmoothness) mKnotPanelKnobSmoothness->SetParamIdx(chBase + 3);
  if (mKnotPanelLink)           mKnotPanelLink          ->SetParamIdx(base + 1);

  if (auto* ui = GetUI()) {
    ui->SetAllControlsDirty();
  }
}
#endif
