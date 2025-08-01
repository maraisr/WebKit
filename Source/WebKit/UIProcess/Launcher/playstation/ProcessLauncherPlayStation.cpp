/*
 * Copyright (C) 2020 Sony Interactive Entertainment Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ProcessLauncher.h"

#include "IPCUtilities.h"
#include <stdint.h>
#include <sys/socket.h>

#if USE(WPE_BACKEND_PLAYSTATION)
#include "ProcessProviderLibWPE.h"
#else
#include <process-launcher.h>
#endif

namespace WebKit {

#if !USE(WPE_BACKEND_PLAYSTATION)
#define MAKE_PROCESS_PATH(x) "/app0/" #x "Process.self"
static const char* defaultProcessPath(ProcessLauncher::ProcessType processType)
{
    switch (processType) {
    case ProcessLauncher::ProcessType::Network:
        return MAKE_PROCESS_PATH(Network);
#if ENABLE(GPU_PROCESS)
    case ProcessLauncher::ProcessType::GPU:
        return MAKE_PROCESS_PATH(GPU);
#endif
    case ProcessLauncher::ProcessType::Web:
    default:
        return MAKE_PROCESS_PATH(Web);
    }
}
#endif

void ProcessLauncher::launchProcess()
{
    IPC::SocketPair socketPair = IPC::createPlatformConnection(IPC::PlatformConnectionOptions::SetCloexecOnServer);

    int sendBufSize = 32 * 1024;
    setsockopt(socketPair.server.value(), SOL_SOCKET, SO_SNDBUF, &sendBufSize, 4);
    setsockopt(socketPair.client.value(), SOL_SOCKET, SO_SNDBUF, &sendBufSize, 4);

    int recvBufSize = 32 * 1024;
    setsockopt(socketPair.server.value(), SOL_SOCKET, SO_RCVBUF, &recvBufSize, 4);
    setsockopt(socketPair.client.value(), SOL_SOCKET, SO_RCVBUF, &recvBufSize, 4);

    char coreProcessIdentifierString[16];
    snprintf(coreProcessIdentifierString, sizeof coreProcessIdentifierString, "%ld", m_launchOptions.processIdentifier.toUInt64());

    char* argv[] = {
        coreProcessIdentifierString,
        nullptr
    };

#if USE(WPE_BACKEND_PLAYSTATION)
    auto appLocalPid = ProcessProviderLibWPE::singleton().launchProcess(m_launchOptions, argv, socketPair.client.value());
#else
    PlayStation::LaunchParam param { socketPair.client.value(), m_launchOptions.userId };
    int32_t appLocalPid = PlayStation::launchProcess(
        !m_launchOptions.processPath.isEmpty() ? m_launchOptions.processPath.utf8().data() : defaultProcessPath(m_launchOptions.processType),
        argv, param);
#endif

    if (appLocalPid < 0) {
#ifndef NDEBUG
        fprintf(stderr, "Failed to launch process. err=0x%08x path=%s\n", appLocalPid, m_launchOptions.processPath.utf8().data());
#endif
        return;
    }

    // We've finished launching the process, message back to the main run loop.
    RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, appLocalPid, serverIdentifier = WTFMove(socketPair.server)] mutable {
        protectedThis->didFinishLaunchingProcess(appLocalPid, IPC::Connection::Identifier { WTFMove(serverIdentifier) });
    });
}

void ProcessLauncher::terminateProcess()
{
    if (!m_processID)
        return;

#if USE(WPE_BACKEND_PLAYSTATION)
    ProcessProviderLibWPE::singleton().kill(m_processID);
#else
    PlayStation::terminateProcess(m_processID);
#endif
}

void ProcessLauncher::platformInvalidate()
{
    m_processID = 0;
}

} // namespace WebKit
