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

#include "config.h"
#include "SourceBufferParser.h"

#if ENABLE(MEDIA_SOURCE)

#include "ContentType.h"
#include "MediaSourceConfiguration.h"
#include "SharedBuffer.h"
#include "SourceBufferParserAVFObjC.h"
#include "SourceBufferParserWebM.h"
#include <pal/spi/cocoa/MediaToolboxSPI.h>
#include <wtf/text/WTFString.h>

#include <pal/cocoa/MediaToolboxSoftLink.h>

namespace WebCore {

MediaPlayerEnums::SupportsType SourceBufferParser::isContentTypeSupported(const ContentType& type)
{
    MediaPlayerEnums::SupportsType supports = MediaPlayerEnums::SupportsType::IsNotSupported;
    supports = std::max(supports, SourceBufferParserWebM::isContentTypeSupported(type));
    supports = std::max(supports, SourceBufferParserAVFObjC::isContentTypeSupported(type));
    return supports;
}

RefPtr<SourceBufferParser> SourceBufferParser::create(const ContentType& type, const MediaSourceConfiguration& configuration)
{
    if (SourceBufferParserWebM::isContentTypeSupported(type) != MediaPlayerEnums::SupportsType::IsNotSupported)
        return SourceBufferParserWebM::create();

    if (SourceBufferParserAVFObjC::isContentTypeSupported(type) != MediaPlayerEnums::SupportsType::IsNotSupported)
        return adoptRef(new SourceBufferParserAVFObjC(configuration));

    return nullptr;
}

static SourceBufferParser::CallOnClientThreadCallback callOnMainThreadCallback()
{
    return [](Function<void()>&& function) {
        callOnMainThread(WTFMove(function));
    };
}

void SourceBufferParser::setCallOnClientThreadCallback(CallOnClientThreadCallback&& callback)
{
    ASSERT(callback);
    m_callOnClientThreadCallback = WTFMove(callback);
}

SourceBufferParser::SourceBufferParser()
    : m_callOnClientThreadCallback(callOnMainThreadCallback())
{
}

void SourceBufferParser::setMinimumAudioSampleDuration(float)
{
}

} // namespace WebCore

#endif // ENABLE(MEDIA_SOURCE)
