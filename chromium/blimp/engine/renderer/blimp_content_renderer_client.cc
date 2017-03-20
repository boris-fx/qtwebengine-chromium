// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/engine/renderer/blimp_content_renderer_client.h"

#include "base/memory/ptr_util.h"
#include "blimp/engine/mojo/blob_channel.mojom.h"
#include "blimp/engine/renderer/blimp_remote_compositor_bridge.h"
#include "blimp/engine/renderer/blob_channel_sender_proxy.h"
#include "blimp/engine/renderer/engine_image_serialization_processor.h"
#include "components/web_cache/renderer/web_cache_impl.h"

namespace blimp {
namespace engine {

BlimpContentRendererClient::BlimpContentRendererClient() {}

BlimpContentRendererClient::~BlimpContentRendererClient() {}

void BlimpContentRendererClient::RenderThreadStarted() {
  web_cache_impl_.reset(new web_cache::WebCacheImpl());
  image_serialization_processor_.reset(new EngineImageSerializationProcessor(
      base::WrapUnique(new BlobChannelSenderProxy)));
}

cc::ImageSerializationProcessor*
BlimpContentRendererClient::GetImageSerializationProcessor() {
  return image_serialization_processor_.get();
}

std::unique_ptr<cc::RemoteCompositorBridge>
BlimpContentRendererClient::CreateRemoteCompositorBridge(
    content::RemoteProtoChannel* remote_proto_channel,
    scoped_refptr<base::SingleThreadTaskRunner> compositor_main_task_runner) {
  return base::MakeUnique<BlimpRemoteCompositorBridge>(
      remote_proto_channel, std::move(compositor_main_task_runner));
}

}  // namespace engine
}  // namespace blimp
