// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/test_tools/mock_quic_dispatcher.h"

namespace net {
namespace tools {
namespace test {

MockQuicDispatcher::MockQuicDispatcher(
    const QuicConfig& config,
    const QuicCryptoServerConfig& crypto_config,
    QuicGuid guid,
    EpollServer* eps)
    : QuicDispatcher(config, crypto_config, QuicSupportedVersions(), guid,
                     eps) {
}
MockQuicDispatcher::~MockQuicDispatcher() {}

}  // namespace test
}  // namespace tools
}  // namespace net
