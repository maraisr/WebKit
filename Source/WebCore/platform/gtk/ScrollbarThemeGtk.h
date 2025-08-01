/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if !USE(GTK4) && USE(CAIRO)

#include "ScrollbarThemeAdwaita.h"

namespace WebCore {

class ScrollbarThemeGtk final : public ScrollbarThemeAdwaita {
public:
    ScrollbarThemeGtk();

    void setUseSystemAppearance(bool);

private:
    bool hasButtons(Scrollbar&) override;
    IntRect backButtonRect(Scrollbar&, ScrollbarPart, bool) override;
    IntRect forwardButtonRect(Scrollbar&, ScrollbarPart, bool) override;
    IntRect trackRect(Scrollbar&, bool) override;

    bool paint(Scrollbar&, GraphicsContext&, const IntRect& damageRect) override;
    ScrollbarButtonPressAction handleMousePressEvent(Scrollbar&, const PlatformMouseEvent&, ScrollbarPart) override;
    int scrollbarThickness(ScrollbarWidth = ScrollbarWidth::Auto, ScrollbarExpansionState = ScrollbarExpansionState::Expanded, OverlayScrollbarSizeRelevancy = OverlayScrollbarSizeRelevancy::IncludeOverlayScrollbarSize) override;
    int minimumThumbLength(Scrollbar&) override;

    void themeChanged() override;
    void updateThemeProperties();

    bool m_hasForwardButtonStartPart : 1;
    bool m_hasForwardButtonEndPart : 1;
    bool m_hasBackButtonStartPart : 1;
    bool m_hasBackButtonEndPart : 1;
    bool m_useSystemAppearance { true };
};

} // namespace WebCore

#endif // !USE(GTK4) && USE(CAIRO)
