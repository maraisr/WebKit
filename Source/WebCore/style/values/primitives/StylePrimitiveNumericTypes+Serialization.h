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

#include "CSSPrimitiveNumericTypes+Serialization.h"
#include "CSSSerializationContext.h"
#include "StylePrimitiveNumericTypes+Conversions.h"
#include "StylePrimitiveNumericTypes.h"
#include "StyleValueTypes.h"

namespace WebCore {
namespace Style {

template<Numeric StyleType> struct Serialize<StyleType> {
    void operator()(StringBuilder& builder, const CSS::SerializationContext& context, const RenderStyle& style, const StyleType& value)
    {
        // FIXME: Do this more efficiently without creating and destroying a CSS::Numeric object.
        CSS::serializationForCSS(builder, context, toCSS(value, style));
    }
};

template<DimensionPercentageNumeric StyleType> struct Serialize<StyleType> {
    void operator()(StringBuilder& builder, const CSS::SerializationContext& context, const RenderStyle& style, const StyleType& value)
    {
        // FIXME: Do this more efficiently without creating and destroying a CSS::Numeric object.
        CSS::serializationForCSS(builder, context, toCSS(value, style));
    }
};

template<Calc StyleType> struct Serialize<StyleType> {
    void operator()(StringBuilder& builder, const CSS::SerializationContext& context, const RenderStyle& style, const StyleType& value)
    {
        // FIXME: Do this more efficiently without creating and destroying a CSS::UnevaluatedCalc object.
        CSS::serializationForCSS(builder, context, toCSS(value, style));
    }
};

} // namespace Style
} // namespace WebCore
