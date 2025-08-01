/*
 * Copyright (C) 2003, 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2005 Nokia. All rights reserved.
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

#include "config.h"
#include "FloatRect.h"

#include "FloatConversion.h"
#include "IntRect.h"
#include <algorithm>
#include <math.h>
#include <wtf/MathExtras.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

FloatRect::FloatRect(const IntRect& r)
    : m_location(r.location())
    , m_size(r.size())
{
}

FloatRect FloatRect::narrowPrecision(double x, double y, double width, double height)
{
    return FloatRect(narrowPrecisionToFloat(x), narrowPrecisionToFloat(y), narrowPrecisionToFloat(width), narrowPrecisionToFloat(height));
}

bool FloatRect::isExpressibleAsIntRect() const
{
    return isWithinIntRange(x()) && isWithinIntRange(y())
        && isWithinIntRange(width()) && isWithinIntRange(height())
        && isWithinIntRange(maxX()) && isWithinIntRange(maxY());
}

bool FloatRect::inclusivelyIntersects(const FloatRect& other) const
{
    return width() >= 0 && height() >= 0 && other.width() >= 0 && other.height() >= 0
        && x() <= other.maxX() && other.x() <= maxX() && y() <= other.maxY() && other.y() <= maxY();
}

bool FloatRect::intersects(const FloatRect& other) const
{
    // Checking emptiness handles negative widths and heights as well as zero.
    return !isEmpty() && !other.isEmpty()
        && x() < other.maxX() && other.x() < maxX()
        && y() < other.maxY() && other.y() < maxY();
}

bool FloatRect::contains(const FloatRect& other) const
{
    return x() <= other.x() && maxX() >= other.maxX()
        && y() <= other.y() && maxY() >= other.maxY();
}

bool FloatRect::contains(const FloatPoint& point, ContainsMode containsMode) const
{
    if (containsMode == InsideOrOnStroke)
        return contains(point.x(), point.y());
    return x() < point.x() && maxX() > point.x() && y() < point.y() && maxY() > point.y();
}

void FloatRect::intersect(const FloatRect& other)
{
    float l = std::max(x(), other.x());
    float t = std::max(y(), other.y());
    float r = std::min(maxX(), other.maxX());
    float b = std::min(maxY(), other.maxY());

    // Return a clean empty rectangle for non-intersecting cases.
    if (l >= r || t >= b) {
        l = 0;
        t = 0;
        r = 0;
        b = 0;
    }

    setLocationAndSizeFromEdges(l, t, r, b);
}

bool FloatRect::edgeInclusiveIntersect(const FloatRect& other)
{
    FloatPoint newLocation(std::max(x(), other.x()), std::max(y(), other.y()));
    FloatPoint newMaxPoint(std::min(maxX(), other.maxX()), std::min(maxY(), other.maxY()));

    bool intersects = true;

    // Return a clean empty rectangle for non-intersecting cases.
    if (newLocation.x() > newMaxPoint.x() || newLocation.y() > newMaxPoint.y()) {
        newLocation = { };
        newMaxPoint = { };
        intersects = false;
    }

    m_location = newLocation;
    m_size = newMaxPoint - newLocation;
    return intersects;
}

void FloatRect::unite(const FloatRect& other)
{
    // Handle empty special cases first.
    if (other.isEmpty())
        return;
    if (isEmpty()) {
        *this = other;
        return;
    }

    uniteEvenIfEmpty(other);
}

void FloatRect::uniteEvenIfEmpty(const FloatRect& other)
{
    float minX = std::min(x(), other.x());
    float minY = std::min(y(), other.y());
    float maxX = std::max(this->maxX(), other.maxX());
    float maxY = std::max(this->maxY(), other.maxY());

    setLocationAndSizeFromEdges(minX, minY, maxX, maxY);
}

void FloatRect::uniteIfNonZero(const FloatRect& other)
{
    // Handle empty special cases first.
    if (other.isZero())
        return;
    if (isZero()) {
        *this = other;
        return;
    }

    uniteEvenIfEmpty(other);
}

void FloatRect::extend(FloatPoint p)
{
    float minX = std::min(x(), p.x());
    float minY = std::min(y(), p.y());
    float maxX = std::max(this->maxX(), p.x());
    float maxY = std::max(this->maxY(), p.y());

    setLocationAndSizeFromEdges(minX, minY, maxX, maxY);
}

void FloatRect::extend(FloatPoint minPoint, FloatPoint maxPoint)
{
    ASSERT(minPoint.x() <= maxPoint.x() && minPoint.y() <= maxPoint.y());

    float minX = std::min(x(), minPoint.x());
    float minY = std::min(y(), minPoint.y());
    float maxX = std::max(this->maxX(), maxPoint.x());
    float maxY = std::max(this->maxY(), maxPoint.y());

    setLocationAndSizeFromEdges(minX, minY, maxX, maxY);
}

void FloatRect::scale(float sx, float sy)
{
    m_location.setX(x() * sx);
    m_location.setY(y() * sy);
    m_size.setWidth(width() * sx);
    m_size.setHeight(height() * sy);
}

FloatRect normalizeRect(const FloatRect& rect)
{
    return FloatRect(std::min(rect.x(), rect.maxX()),
        std::min(rect.y(), rect.maxY()),
        std::max(rect.width(), -rect.width()),
        std::max(rect.height(), -rect.height()));
}

FloatRect encloseRectToDevicePixels(const FloatRect& rect, float deviceScaleFactor)
{
    FloatPoint location = floorPointToDevicePixels(rect.minXMinYCorner(), deviceScaleFactor);
    FloatPoint maxPoint = ceilPointToDevicePixels(rect.maxXMaxYCorner(), deviceScaleFactor);
    return FloatRect(location, maxPoint - location);
}

IntRect enclosingIntRect(const FloatRect& rect)
{
    FloatPoint location = flooredIntPoint(rect.minXMinYCorner());
    FloatPoint maxPoint = ceiledIntPoint(rect.maxXMaxYCorner());
    return IntRect(IntPoint(location), IntSize(maxPoint - location));
}

IntRect enclosingIntRectPreservingEmptyRects(const FloatRect& rect)
{
    // Empty rects with fractional x, y values turn into non-empty rects when converting to enclosing.
    // We want to ensure that empty rects stay empty after the conversion, since some callers
    // prefer this behavior.
    FloatPoint location = flooredIntPoint(rect.minXMinYCorner());
    if (rect.isEmpty())
        return IntRect(IntPoint(location), { });
    FloatPoint maxPoint = ceiledIntPoint(rect.maxXMaxYCorner());
    return IntRect(IntPoint(location), IntSize(maxPoint - location));
}

IntRect roundedIntRect(const FloatRect& rect)
{
    return IntRect(roundedIntPoint(rect.location()), roundedIntSize(rect.size()));
}

TextStream& operator<<(TextStream& ts, const FloatRect &r)
{
    if (ts.hasFormattingFlag(TextStream::Formatting::SVGStyleRect)) {
        // FIXME: callers should use the NumberRespectingIntegers flag.
        return ts << "at ("_s << TextStream::FormatNumberRespectingIntegers(r.x()) << ',' << TextStream::FormatNumberRespectingIntegers(r.y())
            << ") size " << TextStream::FormatNumberRespectingIntegers(r.width()) << "x" << TextStream::FormatNumberRespectingIntegers(r.height());
    }

    return ts << r.location() << ' ' << r.size();
}

Ref<JSON::Object> FloatRect::toJSONObject() const
{
    auto object = JSON::Object::create();

    object->setObject("location"_s, m_location.toJSONObject());
    object->setObject("size"_s, m_size.toJSONObject());

    return object;
}

String FloatRect::toJSONString() const
{
    return toJSONObject()->toJSONString();
}

}
