// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/ipc/media_param_traits.h"

#include "base/strings/stringprintf.h"
#include "ipc/ipc_message_utils.h"
#include "media/base/audio_parameters.h"
#include "media/base/audio_point.h"
#include "media/base/limits.h"
#include "ui/gfx/ipc/geometry/gfx_param_traits.h"
#include "ui/gfx/ipc/gfx_param_traits.h"
#include "ui/gfx/ipc/skia/gfx_skia_param_traits.h"

using media::AudioParameters;
using media::AudioLatency;
using media::ChannelLayout;

namespace IPC {

void ParamTraits<AudioParameters>::GetSize(base::PickleSizer* s,
                                           const AudioParameters& p) {
  GetParamSize(s, p.format());
  GetParamSize(s, p.channel_layout());
  GetParamSize(s, p.sample_rate());
  GetParamSize(s, p.bits_per_sample());
  GetParamSize(s, p.frames_per_buffer());
  GetParamSize(s, p.channels());
  GetParamSize(s, p.effects());
  GetParamSize(s, p.mic_positions());
  GetParamSize(s, p.latency_tag());
}

void ParamTraits<AudioParameters>::Write(base::Pickle* m,
                                         const AudioParameters& p) {
  WriteParam(m, p.format());
  WriteParam(m, p.channel_layout());
  WriteParam(m, p.sample_rate());
  WriteParam(m, p.bits_per_sample());
  WriteParam(m, p.frames_per_buffer());
  WriteParam(m, p.channels());
  WriteParam(m, p.effects());
  WriteParam(m, p.mic_positions());
  WriteParam(m, p.latency_tag());
}

bool ParamTraits<AudioParameters>::Read(const base::Pickle* m,
                                        base::PickleIterator* iter,
                                        AudioParameters* r) {
  AudioParameters::Format format;
  ChannelLayout channel_layout;
  int sample_rate, bits_per_sample, frames_per_buffer, channels, effects;
  std::vector<media::Point> mic_positions;
  AudioLatency::LatencyType latency_tag;

  if (!ReadParam(m, iter, &format) || !ReadParam(m, iter, &channel_layout) ||
      !ReadParam(m, iter, &sample_rate) ||
      !ReadParam(m, iter, &bits_per_sample) ||
      !ReadParam(m, iter, &frames_per_buffer) ||
      !ReadParam(m, iter, &channels) || !ReadParam(m, iter, &effects) ||
      !ReadParam(m, iter, &mic_positions) ||
      !ReadParam(m, iter, &latency_tag)) {
    return false;
  }

  AudioParameters params(format, channel_layout, sample_rate, bits_per_sample,
                         frames_per_buffer);
  params.set_channels_for_discrete(channels);
  params.set_effects(effects);
  params.set_mic_positions(mic_positions);
  params.set_latency_tag(latency_tag);

  *r = params;
  return r->IsValid();
}

void ParamTraits<AudioParameters>::Log(const AudioParameters& p,
                                       std::string* l) {
  l->append(base::StringPrintf("<AudioParameters>"));
}

}  // namespace IPC

// Generate param traits size methods.
#include "ipc/param_traits_size_macros.h"
namespace IPC {
#undef MEDIA_BASE_IPC_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/base/ipc/media_param_traits_macros.h"
}

// Generate param traits write methods.
#include "ipc/param_traits_write_macros.h"
namespace IPC {
#undef MEDIA_BASE_IPC_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/base/ipc/media_param_traits_macros.h"
}  // namespace IPC

// Generate param traits read methods.
#include "ipc/param_traits_read_macros.h"
namespace IPC {
#undef MEDIA_BASE_IPC_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/base/ipc/media_param_traits_macros.h"
}  // namespace IPC

// Generate param traits log methods.
#include "ipc/param_traits_log_macros.h"
namespace IPC {
#undef MEDIA_BASE_IPC_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/base/ipc/media_param_traits_macros.h"
}  // namespace IPC
