/*
 * Copyright (C) 2008 Alex Mathews <possessedpenguinbob@gmail.com>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2021-2022 Apple Inc. All rights reserved.
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
#include "FETile.h"

#include "FETileSoftwareApplier.h"
#include "Filter.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

Ref<FETile> FETile::create(DestinationColorSpace colorSpace)
{
    return adoptRef(*new FETile(colorSpace));
}

FETile::FETile(DestinationColorSpace colorSpace)
    : FilterEffect(FilterEffect::Type::FETile, colorSpace)
{
}

FloatRect FETile::calculateImageRect(const Filter& filter, std::span<const FloatRect>, const FloatRect& primitiveSubregion) const
{
    return filter.maxEffectRect(primitiveSubregion);
}

bool FETile::resultIsAlphaImage(std::span<const Ref<FilterImage>> inputs) const
{
    return inputs[0]->isAlphaImage();
}

std::unique_ptr<FilterEffectApplier> FETile::createSoftwareApplier() const
{
    return FilterEffectApplier::create<FETileSoftwareApplier>(*this);
}

TextStream& FETile::externalRepresentation(TextStream& ts, FilterRepresentation representation) const
{
    ts << indent << "[feTile"_s;
    FilterEffect::externalRepresentation(ts, representation);
    ts << "]\n"_s;
    return ts;
}

} // namespace WebCore
