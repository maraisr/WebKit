/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(GPU_PROCESS) && ENABLE(WEB_AUDIO)

#include "Connection.h"
#include "IPCSemaphore.h"
#include "RemoteAudioDestinationIdentifier.h"
#include <WebCore/SharedMemory.h>
#include <memory>
#include <wtf/CompletionHandler.h>
#include <wtf/HashMap.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/ThreadSafeWeakPtr.h>
#include <wtf/WeakRef.h>

#if PLATFORM(COCOA)
#include <WebCore/AudioOutputUnitAdaptor.h>
#include "SharedCARingBuffer.h"
#endif

namespace WebCore {
#if PLATFORM(COCOA)
class CAAudioStreamDescription;
#endif
class SharedMemoryHandle;
}

namespace WebKit {

class GPUConnectionToWebProcess;
class RemoteAudioDestination;
struct SharedPreferencesForWebProcess;

class RemoteAudioDestinationManager : private IPC::MessageReceiver {
    WTF_MAKE_TZONE_ALLOCATED(RemoteAudioDestinationManager);
    WTF_MAKE_NONCOPYABLE(RemoteAudioDestinationManager);
public:
    RemoteAudioDestinationManager(GPUConnectionToWebProcess&);
    ~RemoteAudioDestinationManager();

    void ref() const final;
    void deref() const final;

    void didReceiveMessageFromWebProcess(IPC::Connection& connection, IPC::Decoder& decoder) { didReceiveMessage(connection, decoder); }

    bool allowsExitUnderMemoryPressure() const;
    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess() const;

private:
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);

    void createAudioDestination(RemoteAudioDestinationIdentifier, const String& inputDeviceId, uint32_t numberOfInputChannels, uint32_t numberOfOutputChannels, float sampleRate, float hardwareSampleRate, IPC::Semaphore&& renderSemaphore, WebCore::SharedMemoryHandle&&, CompletionHandler<void(uint64_t)>&&);
    void deleteAudioDestination(RemoteAudioDestinationIdentifier);
    void startAudioDestination(RemoteAudioDestinationIdentifier, CompletionHandler<void(bool, uint64_t)>&&);
    void stopAudioDestination(RemoteAudioDestinationIdentifier, CompletionHandler<void(bool)>&&);
#if PLATFORM(COCOA)
    void audioSamplesStorageChanged(RemoteAudioDestinationIdentifier, ConsumerSharedCARingBuffer::Handle&&);
#endif
#if PLATFORM(IOS_FAMILY)
    void setSceneIdentifier(RemoteAudioDestinationIdentifier, String&&);
#endif

    HashMap<RemoteAudioDestinationIdentifier, UniqueRef<RemoteAudioDestination>> m_audioDestinations;
    ThreadSafeWeakPtr<GPUConnectionToWebProcess> m_gpuConnectionToWebProcess;
};

} // namespace WebKit;

#endif // ENABLE(GPU_PROCESS) && ENABLE(WEB_AUDIO)
