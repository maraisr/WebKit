/*
 * Copyright (C) 2004, 2005, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2007, 2008 Rob Buis <buis@kde.org>
 * Copyright (C) 2022 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "SVGFEFloodElement.h"

#include "ContainerNodeInlines.h"
#include "FEFlood.h"
#include "RenderElement.h"
#include "RenderStyle.h"
#include "SVGNames.h"
#include "SVGPropertyOwnerRegistry.h"
#include "SVGRenderStyle.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(SVGFEFloodElement);

inline SVGFEFloodElement::SVGFEFloodElement(const QualifiedName& tagName, Document& document)
    : SVGFilterPrimitiveStandardAttributes(tagName, document, makeUniqueRef<PropertyRegistry>(*this))
{
    ASSERT(hasTagName(SVGNames::feFloodTag));
}

Ref<SVGFEFloodElement> SVGFEFloodElement::create(const QualifiedName& tagName, Document& document)
{
    return adoptRef(*new SVGFEFloodElement(tagName, document));
}

bool SVGFEFloodElement::setFilterEffectAttribute(FilterEffect& effect, const QualifiedName& attrName)
{
    CheckedPtr renderer = this->renderer();
    ASSERT(renderer);
    auto& style = renderer->style();

    auto& feFlood = downcast<FEFlood>(effect);
    if (attrName == SVGNames::flood_colorAttr)
        return feFlood.setFloodColor(style.colorResolvingCurrentColor(style.svgStyle().floodColor()));
    if (attrName == SVGNames::flood_opacityAttr)
        return feFlood.setFloodOpacity(style.svgStyle().floodOpacity());

    ASSERT_NOT_REACHED();
    return false;
}

RefPtr<FilterEffect> SVGFEFloodElement::createFilterEffect(const FilterEffectVector&, const GraphicsContext&) const
{
    CheckedPtr renderer = this->renderer();
    if (!renderer)
        return nullptr;

    auto& style = renderer->style();
    auto& svgStyle = style.svgStyle();

    auto color = style.colorWithColorFilter(svgStyle.floodColor());
    float opacity = svgStyle.floodOpacity();

    return FEFlood::create(color, opacity);
}

} // namespace WebCore
