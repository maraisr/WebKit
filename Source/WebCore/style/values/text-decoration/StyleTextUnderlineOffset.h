/*
 * Copyright (C) 2025 Samuel Weinig <sam@webkit.org>
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "StyleLengthWrapper.h"

namespace WebCore {
namespace Style {

// <'text-underline-offset'> = auto | <length-percentage>
// https://drafts.csswg.org/css-text-decor-4/#propdef-text-underline-offset
struct TextUnderlineOffset : LengthWrapperBase<LengthPercentage<>, CSS::Keyword::Auto> {
    using Base::Base;

    float resolve(float fontSize, float autoValue = 0.0f) const;
};

// MARK: - Blending

template<> struct Blending<TextUnderlineOffset> {
    auto canBlend(const TextUnderlineOffset&, const TextUnderlineOffset&, const RenderStyle&, const RenderStyle&) -> bool;
    auto blend(const TextUnderlineOffset&, const TextUnderlineOffset&, const RenderStyle&, const RenderStyle&, const BlendingContext&) -> TextUnderlineOffset;
};

} // namespace Style
} // namespace WebCore

DEFINE_VARIANT_LIKE_CONFORMANCE(WebCore::Style::TextUnderlineOffset)
