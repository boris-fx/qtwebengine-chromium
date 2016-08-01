/*
 * Copyright (c) 2013, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MockResourceClients_h
#define MockResourceClients_h

#include "core/fetch/ImageResourceObserver.h"
#include "core/fetch/Resource.h"
#include "core/fetch/ResourceClient.h"
#include "platform/heap/Handle.h"

namespace blink {

class MockResourceClient : public GarbageCollectedFinalized<MockResourceClient>, public ResourceClient {
    USING_PRE_FINALIZER(MockResourceClient, dispose);
public:
    explicit MockResourceClient(Resource*);
    ~MockResourceClient() override;

    void notifyFinished(Resource*) override;
    String debugName() const override { return "MockResourceClient"; }
    virtual bool notifyFinishedCalled() const { return m_notifyFinishedCalled; }

    virtual void removeAsClient();
    virtual void dispose();

    DECLARE_TRACE();

protected:
    Member<Resource> m_resource;
    bool m_notifyFinishedCalled;
};

class MockImageResourceClient final : public MockResourceClient, public ImageResourceObserver {
public:
    explicit MockImageResourceClient(ImageResource*);
    ~MockImageResourceClient() override;

    void imageNotifyFinished(ImageResource*) override;
    void imageChanged(ImageResource*, const IntRect*) override;

    String debugName() const override { return "MockImageResourceClient"; }

    bool notifyFinishedCalled() const override;

    void removeAsClient() override;
    void dispose() override;

    int imageChangedCount() const { return m_imageChangedCount; }

private:
    int m_imageChangedCount;
    int m_imageNotifyFinishedCount;
};

} // namespace blink

#endif // MockResourceClients_h
