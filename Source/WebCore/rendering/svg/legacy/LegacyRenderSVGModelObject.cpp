/*
 * Copyright (c) 2009 Google Inc. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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
#include "LegacyRenderSVGModelObject.h"

#include "ContainerNodeInlines.h"
#include "LegacyRenderSVGResource.h"
#include "NodeInlines.h"
#include "NotImplemented.h"
#include "RenderElementInlines.h"
#include "RenderLayer.h"
#include "RenderLayerModelObject.h"
#include "RenderObjectInlines.h"
#include "RenderView.h"
#include "SVGElementInlines.h"
#include "SVGLocatable.h"
#include "SVGNames.h"
#include "SVGResourcesCache.h"
#include "ShadowRoot.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(LegacyRenderSVGModelObject);

LegacyRenderSVGModelObject::LegacyRenderSVGModelObject(Type type, SVGElement& element, RenderStyle&& style, OptionSet<SVGModelObjectFlag> typeFlags)
    : RenderElement(type, element, WTFMove(style), { }, typeFlags | SVGModelObjectFlag::IsLegacy | SVGModelObjectFlag::UsesBoundaryCaching)
{
    ASSERT(isLegacyRenderSVGModelObject());
    ASSERT(!isRenderSVGModelObject());
}

LegacyRenderSVGModelObject::~LegacyRenderSVGModelObject() = default;

LayoutRect LegacyRenderSVGModelObject::clippedOverflowRect(const RenderLayerModelObject* repaintContainer, VisibleRectContext context) const
{
    return SVGRenderSupport::clippedOverflowRectForRepaint(*this, repaintContainer, context);
}

auto LegacyRenderSVGModelObject::rectsForRepaintingAfterLayout(const RenderLayerModelObject* repaintContainer, RepaintOutlineBounds repaintOutlineBounds) const -> RepaintRects
{
    auto rects = RepaintRects { clippedOverflowRect(repaintContainer, visibleRectContextForRepaint()) };
    if (repaintOutlineBounds == RepaintOutlineBounds::Yes)
        rects.outlineBoundsRect = outlineBoundsForRepaint(repaintContainer);

    return rects;
}

std::optional<FloatRect> LegacyRenderSVGModelObject::computeFloatVisibleRectInContainer(const FloatRect& rect, const RenderLayerModelObject* container, VisibleRectContext context) const
{
    return SVGRenderSupport::computeFloatVisibleRectInContainer(*this, rect, container, context);
}

void LegacyRenderSVGModelObject::mapLocalToContainer(const RenderLayerModelObject* ancestorContainer, TransformState& transformState, OptionSet<MapCoordinatesMode>, bool* wasFixed) const
{
    SVGRenderSupport::mapLocalToContainer(*this, ancestorContainer, transformState, wasFixed);
}

const RenderElement* LegacyRenderSVGModelObject::pushMappingToContainer(const RenderLayerModelObject* ancestorToStopAt, RenderGeometryMap& geometryMap) const
{
    return SVGRenderSupport::pushMappingToContainer(*this, ancestorToStopAt, geometryMap);
}

static void adjustRectForOutlineAndShadow(const LegacyRenderSVGModelObject& renderer, LayoutRect& rect)
{
    auto shadowRect = rect;
    if (auto& boxShadow = renderer.style().boxShadow(); !boxShadow.isNone())
        Style::adjustRectForShadow(shadowRect, boxShadow);

    auto outlineRect = rect;
    auto outlineSize = LayoutUnit { renderer.outlineStyleForRepaint().outlineSize() };
    if (outlineSize)
        outlineRect.inflate(outlineSize);

    rect = unionRect(shadowRect, outlineRect);
}

// Copied from RenderBox, this method likely requires further refactoring to work easily for both SVG and CSS Box Model content.
// FIXME: This may also need to move into SVGRenderSupport as the RenderBox version depends
// on borderBoundingBox() which SVG RenderBox subclases (like SVGRenderBlock) do not implement.
LayoutRect LegacyRenderSVGModelObject::outlineBoundsForRepaint(const RenderLayerModelObject* repaintContainer, const RenderGeometryMap*) const
{
    LayoutRect box = enclosingLayoutRect(repaintRectInLocalCoordinates());
    adjustRectForOutlineAndShadow(*this, box);

    FloatQuad containerRelativeQuad = localToContainerQuad(FloatRect(box), repaintContainer);
    return LayoutRect(snapRectToDevicePixels(LayoutRect(containerRelativeQuad.boundingBox()), document().deviceScaleFactor()));
}

void LegacyRenderSVGModelObject::boundingRects(Vector<LayoutRect>& rects, const LayoutPoint& accumulatedOffset) const
{
    auto rect = LayoutRect { strokeBoundingBox() };
    rect.moveBy(accumulatedOffset);
    rects.append(rect);
}

void LegacyRenderSVGModelObject::absoluteQuads(Vector<FloatQuad>& quads, bool* wasFixed) const
{
    quads.append(localToAbsoluteQuad(strokeBoundingBox(), UseTransforms, wasFixed));
}

void LegacyRenderSVGModelObject::willBeDestroyed()
{
    SVGResourcesCache::clientDestroyed(*this);
    RenderElement::willBeDestroyed();
}

void LegacyRenderSVGModelObject::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    if (diff == StyleDifference::Layout) {
        invalidateCachedBoundaries();
        if (style().affectsTransform() || (oldStyle && oldStyle->affectsTransform()))
            setNeedsTransformUpdate();
    }
    RenderElement::styleDidChange(diff, oldStyle);
    SVGResourcesCache::clientStyleChanged(*this, diff, oldStyle, style());
}

bool LegacyRenderSVGModelObject::nodeAtPoint(const HitTestRequest&, HitTestResult&, const HitTestLocation&, const LayoutPoint&, HitTestAction)
{
    ASSERT_NOT_REACHED();
    return false;
}

static void getElementCTM(SVGElement* element, AffineTransform& transform)
{
    ASSERT(element);

    RefPtr stopAtElement = SVGLocatable::nearestViewportElement(element);
    ASSERT(stopAtElement);

    AffineTransform localTransform;
    Node* current = element;

    while (RefPtr currentElement = dynamicDowncast<SVGElement>(current)) {
        localTransform = currentElement->renderer()->localToParentTransform();
        transform = localTransform.multiply(transform);
        // For getCTM() computation, stop at the nearest viewport element
        if (currentElement == stopAtElement.get())
            break;

        current = current->parentOrShadowHostNode();
    }
}

// FloatRect::intersects does not consider horizontal or vertical lines (because of isEmpty()).
// So special-case handling of such lines.
static bool intersectsAllowingEmpty(const FloatRect& r, const FloatRect& other)
{
    if (r.isEmpty() && other.isEmpty())
        return false;
    if (r.isEmpty() && !other.isEmpty())
        return (other.contains(r.x(), r.y()) && !other.contains(r.maxX(), r.maxY())) || (!other.contains(r.x(), r.y()) && other.contains(r.maxX(), r.maxY()));
    if (other.isEmpty() && !r.isEmpty())
        return intersectsAllowingEmpty(other, r);
    return r.intersects(other);
}

// One of the element types that can cause graphics to be drawn onto the target canvas. Specifically: circle, ellipse,
// image, line, path, polygon, polyline, rect, text and use.
static bool isGraphicsElement(const RenderElement& renderer)
{
    return renderer.isLegacyRenderSVGShape() || renderer.isRenderSVGText() || renderer.isLegacyRenderSVGImage() || renderer.element()->hasTagName(SVGNames::useTag);
}

// The SVG addFocusRingRects() method adds rects in local coordinates so the default absoluteFocusRingQuads
// returns incorrect values for SVG objects. Overriding this method provides access to the absolute bounds.
void LegacyRenderSVGModelObject::absoluteFocusRingQuads(Vector<FloatQuad>& quads)
{
    quads.append(localToAbsoluteQuad(FloatQuad(repaintRectInLocalCoordinates())));
}
    
bool LegacyRenderSVGModelObject::checkIntersection(RenderElement* renderer, const FloatRect& rect)
{
    if (!renderer || renderer->usedPointerEvents() == PointerEvents::None)
        return false;
    if (!isGraphicsElement(*renderer))
        return false;
    AffineTransform ctm;
    RefPtr svgElement = downcast<SVGElement>(renderer->element());
    getElementCTM(svgElement.get(), ctm);
    ASSERT(svgElement->renderer());
    // FIXME: [SVG] checkEnclosure implementation is inconsistent
    // https://bugs.webkit.org/show_bug.cgi?id=262709
    return intersectsAllowingEmpty(rect, ctm.mapRect(svgElement->checkedRenderer()->repaintRectInLocalCoordinates(RepaintRectCalculation::Accurate)));
}

bool LegacyRenderSVGModelObject::checkEnclosure(RenderElement* renderer, const FloatRect& rect)
{
    if (!renderer || renderer->usedPointerEvents() == PointerEvents::None)
        return false;
    if (!isGraphicsElement(*renderer))
        return false;
    AffineTransform ctm;
    RefPtr svgElement = downcast<SVGElement>(renderer->element());
    getElementCTM(svgElement.get(), ctm);
    ASSERT(svgElement->renderer());
    // FIXME: [SVG] checkEnclosure implementation is inconsistent
    // https://bugs.webkit.org/show_bug.cgi?id=262709
    return rect.contains(ctm.mapRect(svgElement->checkedRenderer()->repaintRectInLocalCoordinates(RepaintRectCalculation::Accurate)));
}

Ref<SVGElement> LegacyRenderSVGModelObject::protectedElement() const
{
    return element();
}

} // namespace WebCore
