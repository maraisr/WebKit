/*
 * Copyright (C) 2009 Alex Milowski (alex@milowski.com). All rights reserved.
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2016 Igalia S.L.
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

#include "config.h"
#include "MathMLTokenElement.h"

#if ENABLE(MATHML)

#include "ContainerNodeInlines.h"
#include "HTTPParsers.h"
#include "MathMLNames.h"
#include "RenderMathMLToken.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(MathMLTokenElement);

using namespace MathMLNames;

MathMLTokenElement::MathMLTokenElement(const QualifiedName& tagName, Document& document)
    : MathMLPresentationElement(tagName, document, TypeFlag::HasCustomStyleResolveCallbacks)
{
}

Ref<MathMLTokenElement> MathMLTokenElement::create(const QualifiedName& tagName, Document& document)
{
    return adoptRef(*new MathMLTokenElement(tagName, document));
}

void MathMLTokenElement::didAttachRenderers()
{
    MathMLPresentationElement::didAttachRenderers();
    if (CheckedPtr mathmlRenderer = dynamicDowncast<RenderMathMLToken>(renderer()))
        mathmlRenderer->updateTokenContent();
}

void MathMLTokenElement::childrenChanged(const ChildChange& change)
{
    MathMLPresentationElement::childrenChanged(change);
    if (CheckedPtr mathmlRenderer = dynamicDowncast<RenderMathMLToken>(renderer()))
        mathmlRenderer->updateTokenContent();
}

RenderPtr<RenderElement> MathMLTokenElement::createElementRenderer(RenderStyle&& style, const RenderTreePosition&)
{
    ASSERT(hasTagName(MathMLNames::miTag) || hasTagName(MathMLNames::mnTag) || hasTagName(MathMLNames::msTag) || hasTagName(MathMLNames::mtextTag));

    return createRenderer<RenderMathMLToken>(RenderObject::Type::MathMLToken, *this, WTFMove(style));
}

bool MathMLTokenElement::childShouldCreateRenderer(const Node& child) const
{
    // The HTML specification defines <mi>, <mo>, <mn>, <ms> and <mtext> as insertion points.
    return StyledElement::childShouldCreateRenderer(child);
}

std::optional<char32_t> MathMLTokenElement::convertToSingleCodePoint(StringView string)
{
    auto codePoints = string.trim(isASCIIWhitespaceWithoutFF<char16_t>).codePoints();
    auto iterator = codePoints.begin();
    if (iterator == codePoints.end())
        return std::nullopt;
    std::optional<char32_t> character = *iterator;
    ++iterator;
    return iterator == codePoints.end() ? character : std::nullopt;
}

}

#endif // ENABLE(MATHML)
