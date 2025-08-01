/*
 * Copyright (C) 2010, 2011 Apple Inc. All rights reserved.
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

#include <wtf/CheckedPtr.h>
#include <wtf/Forward.h>
#include <wtf/RefCounted.h>

namespace WebCore {
class IntRect;
enum class TextDirection : bool;
}

namespace WebKit {

class NativeWebMouseEvent;

struct PlatformPopupMenuData;
struct WebPopupItem;

class WebPopupMenuProxy;

class WebPopupMenuProxyClient : public CanMakeCheckedPtr<WebPopupMenuProxyClient> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(WebPopupMenuProxyClient);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(WebPopupMenuProxyClient);
protected:
    virtual ~WebPopupMenuProxyClient() = default;

public:
    virtual void valueChangedForPopupMenu(WebPopupMenuProxy*, int32_t newSelectedIndex) = 0;
    virtual void setTextFromItemForPopupMenu(WebPopupMenuProxy*, int32_t index) = 0;
    virtual NativeWebMouseEvent* currentlyProcessedMouseDownEvent() = 0;
#if PLATFORM(GTK)
    virtual void failedToShowPopupMenu() = 0;
#endif
};

class WebPopupMenuProxy : public RefCounted<WebPopupMenuProxy> {
public:
    using Client = WebPopupMenuProxyClient;

    virtual ~WebPopupMenuProxy() = default;

    virtual void showPopupMenu(const WebCore::IntRect& rect, WebCore::TextDirection, double pageScaleFactor, const Vector<WebPopupItem>& items, const PlatformPopupMenuData&, int32_t selectedIndex) = 0;
    virtual void hidePopupMenu() = 0;
    virtual void cancelTracking() { }

    void invalidate() { m_client = nullptr; }

protected:
    explicit WebPopupMenuProxy(Client& client)
        : m_client(&client)
    {
    }

    Client* client() const { return m_client.get(); }

private:
    CheckedPtr<Client> m_client;
};

} // namespace WebKit
