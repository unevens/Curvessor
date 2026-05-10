/*
Copyright 2020-2026 Dario Mambro

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

#pragma once

#include "Attachments.h"
#include "WrappedBoolParameter.h"
#include "oversimple/Oversampling.hpp"

// Oversampling parameters + APVTS attachments for Curvessor.
//
// Curvessor needs three independent oversampled streams (wet, dry, sidechain)
// kept in lockstep, so we hold three TOversampling<double> instances and drive
// them together from one pair of parameters (order, linear-phase). The
// attachment owns the cross-thread synchronisation (mutex + suspendProcessing)
// when those settings change, and pushes the resulting latency back to JUCE.

struct OversamplingParameters
{
  juce::RangedAudioParameter* order;
  WrappedBoolParameter linearPhase;
};

template<class Mutex>
class OversamplingAttachments
{
  std::unique_ptr<FloatAttachment> orderAttachment;
  std::unique_ptr<BoolAttachment> linearPhaseAttachment;

public:
  OversamplingAttachments(
    OversamplingParameters& parameters,
    juce::AudioProcessorValueTreeState& apvts,
    juce::AudioProcessor* processor,
    oversimple::TOversampling<double>* wet,
    oversimple::TOversampling<double>* dry,
    oversimple::TOversampling<double>* sidechain,
    oversimple::OversamplingSettings* oversamplingSettings,
    Mutex* oversamplingMutex)
  {
    auto const apply = [processor,
                        wet,
                        dry,
                        sidechain,
                        oversamplingSettings,
                        oversamplingMutex] {
      auto const guard = std::lock_guard<Mutex>(*oversamplingMutex);

      processor->suspendProcessing(true);

      wet->setUseLinearPhase(oversamplingSettings->isUsingLinearPhase);
      dry->setUseLinearPhase(oversamplingSettings->isUsingLinearPhase);
      sidechain->setUseLinearPhase(oversamplingSettings->isUsingLinearPhase);

      wet->setOrder(oversamplingSettings->order);
      dry->setOrder(oversamplingSettings->order);
      sidechain->setOrder(oversamplingSettings->order);

      processor->setLatencySamples(static_cast<int>(wet->getLatency()));

      processor->suspendProcessing(false);
    };

    linearPhaseAttachment = std::make_unique<BoolAttachment>(
      apvts,
      parameters.linearPhase.getID(),
      [this, oversamplingSettings, apply] {
        if (!linearPhaseAttachment) {
          return;
        }
        oversamplingSettings->isUsingLinearPhase =
          linearPhaseAttachment->getValue();
        apply();
      });

    orderAttachment = std::make_unique<FloatAttachment>(
      apvts,
      parameters.order->paramID,
      [this, oversamplingSettings, apply] {
        if (!orderAttachment) {
          return;
        }
        oversamplingSettings->order =
          static_cast<uint32_t>(orderAttachment->getValue());
        apply();
      },
      juce::NormalisableRange<float>(0.f, 5.f, 1.f));
  }
};
