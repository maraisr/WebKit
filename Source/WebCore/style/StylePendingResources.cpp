/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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
#include "StylePendingResources.h"

#include "CSSCursorImageValue.h"
#include "CachedResourceLoader.h"
#include "CursorData.h"
#include "CursorList.h"
#include "DocumentInlines.h"
#include "FillLayer.h"
#include "RenderStyleInlines.h"
#include "SVGURIReference.h"
#include "Settings.h"
#include "ShapeValue.h"
#include "StyleImage.h"
#include "StyleReflection.h"
#include "TransformOperationsBuilder.h"

namespace WebCore {
namespace Style {

// <https://html.spec.whatwg.org/multipage/urls-and-fetching.html#cors-settings-attributes>
enum class LoadPolicy { CORS, NoCORS, Anonymous };
static void loadPendingImage(Document& document, const StyleImage* styleImage, const Element* element, LoadPolicy loadPolicy = LoadPolicy::NoCORS)
{
    if (!styleImage || !styleImage->isPending())
        return;

    bool isInUserAgentShadowTree = element && element->isInUserAgentShadowTree();
    ResourceLoaderOptions options = CachedResourceLoader::defaultCachedResourceOptions();
    options.contentSecurityPolicyImposition = isInUserAgentShadowTree ? ContentSecurityPolicyImposition::SkipPolicyCheck : ContentSecurityPolicyImposition::DoPolicyCheck;

    if (!isInUserAgentShadowTree && document.settings().useAnonymousModeWhenFetchingMaskImages()) {
        switch (loadPolicy) {
        case LoadPolicy::Anonymous:
            options.storedCredentialsPolicy = StoredCredentialsPolicy::DoNotUse;
            [[fallthrough]];
        case LoadPolicy::CORS:
            options.mode = FetchOptions::Mode::Cors;
            options.credentials = FetchOptions::Credentials::SameOrigin;
            options.sameOriginDataURLFlag = SameOriginDataURLFlag::Set;
            break;
        case LoadPolicy::NoCORS:
            break;
        }
    }

    const_cast<StyleImage&>(*styleImage).load(document.cachedResourceLoader(), options);
}

void loadPendingResources(RenderStyle& style, Document& document, const Element* element)
{
    for (auto* backgroundLayer = &style.backgroundLayers(); backgroundLayer; backgroundLayer = backgroundLayer->next())
        loadPendingImage(document, backgroundLayer->image(), element);

    if (auto* contentData = style.content().tryData()) {
        for (auto& contentItem : contentData->list) {
            WTF::switchOn(contentItem,
                [&](const Style::Content::Image& image) {
                    loadPendingImage(document, image.image.ptr(), element);
                },
                [](const auto&) { }
            );
        }
    }

    if (auto* cursorList = style.cursors()) {
        for (size_t i = 0; i < cursorList->size(); ++i)
            loadPendingImage(document, cursorList->at(i).image(), element);
    }

    loadPendingImage(document, style.listStyleImage(), element);
    loadPendingImage(document, style.borderImageSource(), element);
    loadPendingImage(document, style.maskBorderSource(), element);

    if (auto* reflection = style.boxReflect())
        loadPendingImage(document, reflection->mask().image(), element);

    // Masking operations may be sensitive to timing attacks that can be used to reveal the pixel data of
    // the image used as the mask. As a means to mitigate such attacks CSS mask images and shape-outside
    // images are retrieved in "Anonymous" mode, which uses a potentially CORS-enabled fetch.
    for (auto* maskLayer = &style.maskLayers(); maskLayer; maskLayer = maskLayer->next())
        loadPendingImage(document, maskLayer->image(), element, LoadPolicy::CORS);

    if (style.shapeOutside())
        loadPendingImage(document, style.shapeOutside()->image(), element, LoadPolicy::Anonymous);

    // Are there other pseudo-elements that need resource loading? 
    if (auto* firstLineStyle = style.getCachedPseudoStyle({ PseudoId::FirstLine }))
        loadPendingResources(*firstLineStyle, document, element);
}

}
}
