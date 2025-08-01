/*
 * Copyright (C) 2010-2025 Apple Inc. All rights reserved.
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

#include "APIObject.h"
#include "ImageOptions.h"
#include <JavaScriptCore/JSBase.h>
#include <wtf/Forward.h>
#include <wtf/RefPtr.h>
#include <wtf/WeakPtr.h>

namespace WebCore {
class IntRect;
class Range;
struct SimpleRange;
}

namespace WebKit {

class InjectedBundleNodeHandle;
class InjectedBundleScriptWorld;
class WebImage;

class InjectedBundleRangeHandle : public API::ObjectImpl<API::Object::Type::BundleRangeHandle>, public CanMakeWeakPtr<InjectedBundleRangeHandle> {
public:
    static RefPtr<InjectedBundleRangeHandle> getOrCreate(JSContextRef, JSObjectRef);
    static RefPtr<InjectedBundleRangeHandle> getOrCreate(WebCore::Range*);

    virtual ~InjectedBundleRangeHandle();

    Ref<InjectedBundleNodeHandle> document();

    WebCore::IntRect boundingRectInWindowCoordinates() const;
    RefPtr<WebImage> renderedImage(SnapshotOptions);
    String text() const;

    WebCore::Range& coreRange() const { return m_range; }

private:
    InjectedBundleRangeHandle(WebCore::Range&);

    const Ref<WebCore::Range> m_range;
};

RefPtr<InjectedBundleRangeHandle> createHandle(const std::optional<WebCore::SimpleRange>&);

} // namespace WebKit

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::InjectedBundleRangeHandle)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::BundleRangeHandle; }
SPECIALIZE_TYPE_TRAITS_END()
