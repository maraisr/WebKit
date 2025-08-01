/*
 * Copyright (C) 2024 Igalia S.L.
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

#include "WebAutomationSession.h"
#include "WebAutomationSessionMacros.h"
#include "WebPageProxy.h"

namespace WebKit {

#if ENABLE(WEBDRIVER_MOUSE_INTERACTIONS)
void platformSimulateMouseInteractionLibWPE(WebPageProxy&, MouseInteraction, MouseButton, const WebCore::IntPoint& locationInView, OptionSet<WebEventModifier> keyModifiers, const String& pointerType, unsigned& currentModifiers);
#endif
#if ENABLE(WEBDRIVER_KEYBOARD_INTERACTIONS)
void platformSimulateKeyboardInteractionLibWPE(WebPageProxy&, KeyboardInteraction, Variant<VirtualKey, CharKey>&&, unsigned& currentModifiers);
void platformSimulateKeySequenceLibWPE(WebPageProxy&, const String& keySequence, unsigned& currentModifiers);
#endif
#if ENABLE(WEBDRIVER_WHEEL_INTERACTIONS)
void platformSimulateWheelInteractionLibWPE(WebPageProxy&, const WebCore::IntPoint& location, const WebCore::IntSize& delta);
#endif

} // namespace WebKit
