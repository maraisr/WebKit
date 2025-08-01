/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2018 Google Inc. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011-2012. All rights reserved.
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
 *
 */

#include "config.h"
#include "RenderReplaced.h"

#include "BackgroundPainter.h"
#include "BorderShape.h"
#include "DocumentInlines.h"
#include "DocumentMarkerController.h"
#include "ElementRuleCollector.h"
#include "FloatRoundedRect.h"
#include "GraphicsContext.h"
#include "HTMLElement.h"
#include "HTMLImageElement.h"
#include "HTMLParserIdioms.h"
#include "HighlightRegistry.h"
#include "InlineIteratorBox.h"
#include "InlineIteratorLineBoxInlines.h"
#include "LayoutRepainter.h"
#include "LineSelection.h"
#include "LocalFrame.h"
#include "RenderBlock.h"
#include "RenderBoxInlines.h"
#include "RenderChildIterator.h"
#include "RenderElementInlines.h"
#include "RenderFlexibleBox.h"
#include "RenderFragmentedFlow.h"
#include "RenderHTMLCanvas.h"
#include "RenderHighlight.h"
#include "RenderImage.h"
#include "RenderLayer.h"
#include "RenderLayoutState.h"
#include "RenderObjectInlines.h"
#include "RenderStyleInlines.h"
#include "RenderStyleSetters.h"
#include "RenderTheme.h"
#include "RenderVideo.h"
#include "RenderView.h"
#include "RenderedDocumentMarker.h"
#include "Settings.h"
#include "VisiblePosition.h"
#include <wtf/StackStats.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/TypeCasts.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderReplaced);

const int cDefaultWidth = 300;
const int cDefaultHeight = 150;

RenderReplaced::RenderReplaced(Type type, Element& element, RenderStyle&& style, OptionSet<ReplacedFlag> flags)
    : RenderBox(type, element, WTFMove(style), { }, flags)
    , m_intrinsicSize(cDefaultWidth, cDefaultHeight)
{
    ASSERT(element.isReplaced(this->style()) || type == Type::Image);
    setBlockLevelReplacedOrAtomicInline(true);
    ASSERT(isRenderReplaced());
}

RenderReplaced::RenderReplaced(Type type, Element& element, RenderStyle&& style, const LayoutSize& intrinsicSize, OptionSet<ReplacedFlag> flags)
    : RenderBox(type, element, WTFMove(style), { }, flags)
    , m_intrinsicSize(intrinsicSize)
{
    ASSERT(element.isReplaced(this->style()) || type == Type::Image);
    setBlockLevelReplacedOrAtomicInline(true);
    ASSERT(isRenderReplaced());
}

RenderReplaced::RenderReplaced(Type type, Document& document, RenderStyle&& style, const LayoutSize& intrinsicSize, OptionSet<ReplacedFlag> flags)
    : RenderBox(type, document, WTFMove(style), { }, flags)
    , m_intrinsicSize(intrinsicSize)
{
    setBlockLevelReplacedOrAtomicInline(true);
    ASSERT(isRenderReplaced());
}

RenderReplaced::~RenderReplaced() = default;

void RenderReplaced::willBeDestroyed()
{
    if (!renderTreeBeingDestroyed() && parent())
        parent()->dirtyLineFromChangedChild();

    RenderBox::willBeDestroyed();
}

void RenderReplaced::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    RenderBox::styleDidChange(diff, oldStyle);
    auto previousUsedZoom = oldStyle ? oldStyle->usedZoom() : RenderStyle::initialZoom();
    if (previousUsedZoom != style().usedZoom())
        intrinsicSizeChanged();
}

static bool shouldRepaintOnSizeChange(RenderReplaced& renderer)
{
    if (is<RenderHTMLCanvas>(renderer))
        return true;
    if (auto* renderImage = dynamicDowncast<RenderImage>(renderer); renderImage && !is<RenderMedia>(*renderImage) && !renderImage->isShowingMissingOrImageError())
        return true;
    return false;
}

void RenderReplaced::layout()
{
    StackStats::LayoutCheckPoint layoutCheckPoint;
    ASSERT(needsLayout());
    
    LayoutRepainter repainter(*this);

    LayoutRect oldContentRect = replacedContentRect();
    
    setHeight(minimumReplacedHeight());

    updateLogicalWidth();
    updateLogicalHeight();

    clearOverflow();
    addVisualEffectOverflow();
    updateLayerTransform();
    invalidateBackgroundObscurationStatus();
    repainter.repaintAfterLayout();
    clearNeedsLayout();

    if (replacedContentRect() != oldContentRect) {
        setNeedsPreferredWidthsUpdate();
        if (shouldRepaintOnSizeChange(*this))
            repaint();
    }
}

void RenderReplaced::intrinsicSizeChanged()
{
    int scaledWidth = static_cast<int>(cDefaultWidth * style().usedZoom());
    int scaledHeight = static_cast<int>(cDefaultHeight * style().usedZoom());
    m_intrinsicSize = IntSize(scaledWidth, scaledHeight);
    setNeedsLayoutAndPreferredWidthsUpdate();
}

bool RenderReplaced::shouldDrawSelectionTint() const
{
    return selectionState() != HighlightState::None && !document().printing();
}

inline static bool contentContainsReplacedElement(const Vector<WeakPtr<RenderedDocumentMarker>>& markers, const Element& element)
{
    for (auto& marker : markers) {
        if (marker->type() == DocumentMarkerType::DraggedContent) {
            if (std::get<RefPtr<Node>>(marker->data()) == &element)
                return true;
        } else if (marker->type() == DocumentMarkerType::TransparentContent) {
            if (std::get<DocumentMarker::TransparentContentData>(marker->data()).node == &element)
                return true;
        }
    }
    return false;
}

Color RenderReplaced::calculateHighlightColor() const
{
    RenderHighlight renderHighlight;
#if ENABLE(APP_HIGHLIGHTS)
    if (auto appHighlightRegistry = document().appHighlightRegistryIfExists()) {
        if (appHighlightRegistry->highlightsVisibility() == HighlightVisibility::Visible) {
            for (auto& highlight : appHighlightRegistry->map()) {
                for (auto& highlightRange : highlight.value->highlightRanges()) {
                    if (!renderHighlight.setRenderRange(highlightRange))
                        continue;

                    auto state = renderHighlight.highlightStateForRenderer(*this);
                    if (!isHighlighted(state, renderHighlight))
                        continue;

                    OptionSet<StyleColorOptions> styleColorOptions = { StyleColorOptions::UseSystemAppearance };
                    return theme().annotationHighlightColor(styleColorOptions);
                }
            }
        }
    }
#endif
    if (auto highlightRegistry = document().highlightRegistryIfExists()) {
        for (auto& highlight : highlightRegistry->map()) {
            for (auto& highlightRange : highlight.value->highlightRanges()) {
                if (!renderHighlight.setRenderRange(highlightRange))
                    continue;

                auto state = renderHighlight.highlightStateForRenderer(*this);
                if (!isHighlighted(state, renderHighlight))
                    continue;

                if (auto highlightStyle = getCachedPseudoStyle({ PseudoId::Highlight, highlight.key }, &style()))
                    return highlightStyle->colorResolvingCurrentColor(highlightStyle->backgroundColor());
            }
        }
    }

    if (document().settings().scrollToTextFragmentEnabled()) {
        if (auto highlightRegistry = document().fragmentHighlightRegistryIfExists()) {
            for (auto& highlight : highlightRegistry->map()) {
                for (auto& highlightRange : highlight.value->highlightRanges()) {
                    if (!renderHighlight.setRenderRange(highlightRange))
                        continue;

                    auto state = renderHighlight.highlightStateForRenderer(*this);
                    if (!isHighlighted(state, renderHighlight))
                        continue;

                    OptionSet<StyleColorOptions> styleColorOptions = { StyleColorOptions::UseSystemAppearance };
                    return theme().annotationHighlightColor(styleColorOptions);
                }
            }
        }
    }
    return Color();
}

void RenderReplaced::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if (!shouldPaint(paintInfo, paintOffset))
        return;

    LayoutPoint adjustedPaintOffset = paintOffset + location();

    if (paintInfo.phase == PaintPhase::EventRegion) {
#if ENABLE(INTERACTION_REGIONS_IN_EVENT_REGION)
        if (isRenderOrLegacyRenderSVGRoot() && !isSkippedContentRoot(*this))
            paintReplaced(paintInfo, adjustedPaintOffset);
        else if (visibleToHitTesting()) {
#else
        if (visibleToHitTesting()) {
#endif
            auto borderRect = LayoutRect(adjustedPaintOffset, size());
            auto borderShape = BorderShape::shapeForBorderRect(style(), borderRect);
            paintInfo.eventRegionContext()->unite(borderShape.deprecatedPixelSnappedRoundedRect(document().deviceScaleFactor()), *this, style());
        }
        return;
    }

    if (paintInfo.phase == PaintPhase::Accessibility) {
        paintInfo.accessibilityRegionContext()->takeBounds(*this, adjustedPaintOffset);
        return;
    }

    SetLayoutNeededForbiddenScope scope(*this);

    GraphicsContextStateSaver savedGraphicsContext(paintInfo.context(), false);
    if (element() && element()->parentOrShadowHostElement()) {
        auto* parentContainer = element()->parentOrShadowHostElement();
        ASSERT(parentContainer);
        CheckedPtr markers = document().markersIfExists();
        if (markers) {
            if (contentContainsReplacedElement(markers->markersFor(*parentContainer, DocumentMarkerType::DraggedContent), *element())) {
                savedGraphicsContext.save();
                paintInfo.context().setAlpha(0.25);
            }
            if (contentContainsReplacedElement(markers->markersFor(*parentContainer, DocumentMarkerType::TransparentContent), *element())) {
                savedGraphicsContext.save();
                paintInfo.context().setAlpha(0.0);
            }
        }
    }

    if (hasVisibleBoxDecorations() && paintInfo.phase == PaintPhase::Foreground)
        paintBoxDecorations(paintInfo, adjustedPaintOffset);
    
    if (paintInfo.phase == PaintPhase::Mask) {
        paintMask(paintInfo, adjustedPaintOffset);
        return;
    }

    if (paintInfo.phase == PaintPhase::ClippingMask && style().usedVisibility() == Visibility::Visible) {
        paintClippingMask(paintInfo, adjustedPaintOffset);
        return;
    }

    LayoutRect paintRect = LayoutRect(adjustedPaintOffset, size());
    if (paintInfo.phase == PaintPhase::Outline || paintInfo.phase == PaintPhase::SelfOutline) {
        if (style().outlineWidth())
            paintOutline(paintInfo, paintRect);
        return;
    }

    if (paintInfo.phase != PaintPhase::Foreground && paintInfo.phase != PaintPhase::Selection)
        return;
    
    if (!paintInfo.shouldPaintWithinRoot(*this))
        return;
    
    Color highlightColor;
    if (!document().printing() && !paintInfo.paintBehavior.contains(PaintBehavior::ExcludeSelection))
        highlightColor = calculateHighlightColor();
    
    bool drawSelectionTint = shouldDrawSelectionTint();
    if (paintInfo.phase == PaintPhase::Selection) {
        if (selectionState() == HighlightState::None)
            return;
        drawSelectionTint = false;
    }

    bool completelyClippedOut = false;
    if (style().hasBorderRadius()) {
        completelyClippedOut = size().isEmpty();
        if (!completelyClippedOut) {
            // Push a clip if we have a border radius, since we want to round the foreground content that gets painted.
            paintInfo.context().save();
            clipToContentBoxShape(paintInfo.context(), adjustedPaintOffset, document().deviceScaleFactor());
        }
    }

    if (!completelyClippedOut) {
        if (!isSkippedContentRoot(*this))
            paintReplaced(paintInfo, adjustedPaintOffset);

        if (style().hasBorderRadius())
            paintInfo.context().restore();
    }
        
    // The selection tint never gets clipped by border-radius rounding, since we want it to run right up to the edges of
    // surrounding content.
    if (drawSelectionTint) {
        LayoutRect selectionPaintingRect = localSelectionRect();
        selectionPaintingRect.moveBy(adjustedPaintOffset);
        paintInfo.context().fillRect(snappedIntRect(selectionPaintingRect), selectionBackgroundColor());
    }
    
    if (highlightColor.isVisible()) {
        auto selectionPaintingRect = localSelectionRect(false);
        selectionPaintingRect.moveBy(adjustedPaintOffset);
        paintInfo.context().fillRect(snappedIntRect(selectionPaintingRect), highlightColor);
    }
}

bool RenderReplaced::shouldPaint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if ((paintInfo.paintBehavior.contains(PaintBehavior::ExcludeSelection)) && isSelected())
        return false;

    if (paintInfo.paintBehavior.contains(PaintBehavior::ExcludeReplacedContentExceptForIFrames) && !isRenderIFrame())
        return false;

    if (paintInfo.phase != PaintPhase::Foreground
        && paintInfo.phase != PaintPhase::Outline
        && paintInfo.phase != PaintPhase::SelfOutline
        && paintInfo.phase != PaintPhase::Selection
        && paintInfo.phase != PaintPhase::Mask
        && paintInfo.phase != PaintPhase::ClippingMask
        && paintInfo.phase != PaintPhase::EventRegion
        && paintInfo.phase != PaintPhase::Accessibility)
        return false;

    if (!paintInfo.shouldPaintWithinRoot(*this))
        return false;
        
    // if we're invisible or haven't received a layout yet, then just bail.
    if (style().usedVisibility() != Visibility::Visible)
        return false;
    
    LayoutRect paintRect(visualOverflowRect());
    paintRect.moveBy(paintOffset + location());

    // Early exit if the element touches the edges.
    LayoutUnit top = paintRect.y();
    LayoutUnit bottom = paintRect.maxY();

    LayoutRect localRepaintRect = paintInfo.rect;
    if (paintRect.x() >= localRepaintRect.maxX() || paintRect.maxX() <= localRepaintRect.x())
        return false;

    if (top >= localRepaintRect.maxY() || bottom <= localRepaintRect.y())
        return false;

    return true;
}

bool RenderReplaced::hasReplacedLogicalHeight() const
{
    if (style().logicalHeight().isAuto())
        return false;

    if (style().logicalHeight().isFixed())
        return true;

    if (style().logicalHeight().isPercentOrCalculated())
        return !hasAutoHeightOrContainingBlockWithAutoHeight();

    if (style().logicalHeight().isIntrinsic())
        return !style().hasAspectRatio();

    return false;
}

bool RenderReplaced::setNeedsLayoutIfNeededAfterIntrinsicSizeChange()
{
    setNeedsPreferredWidthsUpdate();

    // If the actual area occupied by the image has changed and it is not constrained by style then a layout is required.
    bool imageSizeIsConstrained = style().logicalWidth().isSpecified() && style().logicalHeight().isSpecified()
        && !style().logicalMinWidth().isIntrinsic() && !style().logicalMaxWidth().isIntrinsic()
        && !hasAutoHeightOrContainingBlockWithAutoHeight(UpdatePercentageHeightDescendants::No);

    // FIXME: We only need to recompute the containing block's preferred size
    // if the containing block's size depends on the image's size (i.e., the container uses shrink-to-fit sizing).
    // There's no easy way to detect that shrink-to-fit is needed, always force a layout.
    bool containingBlockNeedsToRecomputePreferredSize =
        style().logicalWidth().isPercentOrCalculated()
        || style().logicalMaxWidth().isPercentOrCalculated()
        || style().logicalMinWidth().isPercentOrCalculated();

    // Flex and grid layout use the intrinsic image width/height even if width/height are specified.
    if (!imageSizeIsConstrained || containingBlockNeedsToRecomputePreferredSize || isFlexItem() || isGridItem()) {
        setNeedsLayout();
        return true;
    }

    return false;
}

static bool isVideoWithDefaultObjectSize(const RenderReplaced* maybeVideo)
{
#if ENABLE(VIDEO)
    if (auto* video = dynamicDowncast<RenderVideo>(maybeVideo))
        return video->hasDefaultObjectSize();
#else
    UNUSED_PARAM(maybeVideo);
#endif
    return false;
} 

void RenderReplaced::computeAspectRatioInformationForRenderBox(RenderBox* contentRenderer, FloatSize& constrainedSize, FloatSize& preferredAspectRatio) const
{
    FloatSize intrinsicSize;
    if (shouldApplySizeOrInlineSizeContainment())
        RenderReplaced::computeIntrinsicSizeAndPreferredAspectRatio(intrinsicSize, preferredAspectRatio);
    else if (contentRenderer) {
        contentRenderer->computeIntrinsicSizeAndPreferredAspectRatio(intrinsicSize, preferredAspectRatio);

        if (style().aspectRatio().isRatio() || (style().aspectRatio().isAutoAndRatio() && preferredAspectRatio.isEmpty()))
            preferredAspectRatio = FloatSize::narrowPrecision(style().aspectRatioWidth().value, style().aspectRatioHeight().value);

        // Handle zoom & vertical writing modes here, as the embedded document doesn't know about them.
        intrinsicSize.scale(style().usedZoom());

        if (auto* image = dynamicDowncast<RenderImage>(*this))
            intrinsicSize.scale(image->imageDevicePixelRatio());

        // Update our intrinsic size to match what the content renderer has computed, so that when we
        // constrain the size below, the correct intrinsic size will be obtained for comparison against
        // min and max widths.
        if (!preferredAspectRatio.isEmpty() && !intrinsicSize.isZero())
            m_intrinsicSize = LayoutSize(intrinsicSize);

        if (!isHorizontalWritingMode()) {
            if (!preferredAspectRatio.isEmpty())
                preferredAspectRatio = preferredAspectRatio.transposedSize();
            intrinsicSize = intrinsicSize.transposedSize();
        }
    } else {
        computeIntrinsicSizeAndPreferredAspectRatio(intrinsicSize, preferredAspectRatio);
        if (!preferredAspectRatio.isEmpty() && !intrinsicSize.isZero())
            m_intrinsicSize = LayoutSize(isHorizontalWritingMode() ? intrinsicSize : intrinsicSize.transposedSize());
    }
    constrainedSize = intrinsicSize;
}

void RenderReplaced::computeIntrinsicSizesConstrainedByTransferredMinMaxSizes(RenderBox* contentRenderer, FloatSize& intrinsicSize, FloatSize& intrinsicRatio) const
{
    computeAspectRatioInformationForRenderBox(contentRenderer, intrinsicSize, intrinsicRatio);

    // Now constrain the intrinsic size along each axis according to minimum and maximum width/heights along the
    // opposite axis. So for example a maximum width that shrinks our width will result in the height we compute here
    // having to shrink in order to preserve the aspect ratio. Because we compute these values independently along
    // each axis, the final returned size may in fact not preserve the aspect ratio.
    auto& style = this->style();
    auto computedLogicalHeight = style.logicalHeight();
    bool logicalHeightBehavesAsAuto = computedLogicalHeight.isAuto() || (computedLogicalHeight.isPercentOrCalculated() && !percentageLogicalHeightIsResolvable());
    if (!intrinsicRatio.isZero() && style.logicalWidth().isAuto() && logicalHeightBehavesAsAuto) {
        auto removeBorderAndPaddingFromMinMaxSizes = [](LayoutUnit& minSize, LayoutUnit &maxSize, LayoutUnit borderAndPadding) {
            minSize = std::max(0_lu, minSize - borderAndPadding);
            maxSize = std::max(0_lu, maxSize - borderAndPadding);
        };

        auto [minLogicalWidth, maxLogicalWidth] = computeMinMaxLogicalWidthFromAspectRatio();
        removeBorderAndPaddingFromMinMaxSizes(minLogicalWidth, maxLogicalWidth, borderAndPaddingLogicalWidth());

        auto [minLogicalHeight, maxLogicalHeight] = computeMinMaxLogicalHeightFromAspectRatio();
        removeBorderAndPaddingFromMinMaxSizes(minLogicalHeight, maxLogicalHeight, borderAndPaddingLogicalHeight());

        intrinsicSize.setWidth(std::clamp(LayoutUnit { intrinsicSize.width() }, minLogicalWidth, maxLogicalWidth));
        intrinsicSize.setHeight(std::clamp(LayoutUnit { intrinsicSize.height() }, minLogicalHeight, maxLogicalHeight));
    }
}

LayoutRect RenderReplaced::replacedContentRect(const LayoutSize& intrinsicSize) const
{
    LayoutRect contentRect = contentBoxRect();
    if (intrinsicSize.isEmpty())
        return contentRect;

    ObjectFit objectFit = style().objectFit();

    LayoutRect finalRect = contentRect;
    switch (objectFit) {
    case ObjectFit::Contain:
    case ObjectFit::ScaleDown:
    case ObjectFit::Cover:
        finalRect.setSize(finalRect.size().fitToAspectRatio(intrinsicSize, objectFit == ObjectFit::Cover ? AspectRatioFitGrow : AspectRatioFitShrink));
        if (objectFit != ObjectFit::ScaleDown || finalRect.width() <= intrinsicSize.width())
            break;
        [[fallthrough]];
    case ObjectFit::None:
        finalRect.setSize(intrinsicSize);
        break;
    case ObjectFit::Fill:
        break;
    }

    auto& objectPosition = style().objectPosition();

    LayoutUnit xOffset = Style::evaluate(objectPosition.x, contentRect.width() - finalRect.width());
    LayoutUnit yOffset = Style::evaluate(objectPosition.y, contentRect.height() - finalRect.height());

    finalRect.move(xOffset, yOffset);

    return finalRect;
}

double RenderReplaced::computeIntrinsicAspectRatio() const
{
    FloatSize intrinsicRatio;
    FloatSize intrinsicSize;
    computeAspectRatioInformationForRenderBox(embeddedContentBox(), intrinsicSize, intrinsicRatio);
    return intrinsicRatio.aspectRatioDouble();
}

void RenderReplaced::computeIntrinsicSizeAndPreferredAspectRatio(FloatSize& intrinsicSize, FloatSize& intrinsicRatio) const
{
    // If there's an embeddedContentBox() of a remote, referenced document available, this code-path should never be used.
    ASSERT(!embeddedContentBox() || shouldApplySizeOrInlineSizeContainment());
    intrinsicSize = FloatSize(intrinsicLogicalWidth(), intrinsicLogicalHeight());

    if (style().hasAspectRatio()) {
        intrinsicRatio = FloatSize::narrowPrecision(style().aspectRatioLogicalWidth().value, style().aspectRatioLogicalHeight().value);
        if (style().aspectRatio().isRatio() || isVideoWithDefaultObjectSize(this))
            return;
    }
    // Figure out if we need to compute an intrinsic ratio.
    if (!RenderBox::hasIntrinsicAspectRatio() && !isRenderOrLegacyRenderSVGRoot())
        return;

    // After supporting contain-intrinsic-size, the intrinsicSize of size containment is not always empty.
    if (intrinsicSize.isEmpty() || shouldApplySizeContainment())
        return;

    intrinsicRatio = { intrinsicSize.width(), intrinsicSize.height() };
}

LayoutUnit RenderReplaced::computeConstrainedLogicalWidth() const
{
    // The aforementioned 'constraint equation' used for block-level, non-replaced
    // elements in normal flow:
    // 'margin-left' + 'border-left-width' + 'padding-left' + 'width' +
    // 'padding-right' + 'border-right-width' + 'margin-right' = width of
    // containing block
    // see https://www.w3.org/TR/CSS22/visudet.html#blockwidth
    LayoutUnit logicalWidth = containingBlock()->contentBoxLogicalWidth();
    
    // This solves above equation for 'width' (== logicalWidth).
    LayoutUnit marginStart = Style::evaluateMinimum(style().marginStart(), logicalWidth);
    LayoutUnit marginEnd = Style::evaluateMinimum(style().marginEnd(), logicalWidth);

    return std::max(0_lu, (logicalWidth - (marginStart + marginEnd + borderLeft() + borderRight() + paddingLeft() + paddingRight())));
}

void RenderReplaced::computeAspectRatioAdjustedIntrinsicLogicalWidths(LayoutUnit& minLogicalWidth, LayoutUnit& maxLogicalWidth) const
{
    computeIntrinsicLogicalWidths(minLogicalWidth, maxLogicalWidth);

    if (!hasIntrinsicAspectRatio())
        return;

    auto& style = this->style();
    auto computedAspectRatio = computeIntrinsicAspectRatio();
    auto computedIntrinsicLogicalWidth = minLogicalWidth;

    if (auto fixedLogicalHeight = style.logicalHeight().tryFixed())
        computedIntrinsicLogicalWidth = fixedLogicalHeight->value * computedAspectRatio;

    if (auto fixedLogicalMaxHeight = style.logicalMaxHeight().tryFixed())
        computedIntrinsicLogicalWidth = std::min(computedIntrinsicLogicalWidth, LayoutUnit { fixedLogicalMaxHeight->value * computedAspectRatio });

    if (auto fixedLogicalMinHeight = style.logicalMinHeight().tryFixed())
        computedIntrinsicLogicalWidth = std::max(computedIntrinsicLogicalWidth, LayoutUnit { fixedLogicalMinHeight->value * computedAspectRatio });

    minLogicalWidth = computedIntrinsicLogicalWidth;
    maxLogicalWidth = minLogicalWidth;
}

static inline LayoutUnit resolveWidthForRatio(LayoutUnit borderAndPaddingLogicalHeight, LayoutUnit borderAndPaddingLogicalWidth, LayoutUnit logicalHeight, double aspectRatio, BoxSizing boxSizing)
{
    if (boxSizing == BoxSizing::BorderBox)
        return LayoutUnit((logicalHeight + borderAndPaddingLogicalHeight) * aspectRatio) - borderAndPaddingLogicalWidth;
    return LayoutUnit(logicalHeight * aspectRatio);
}

static inline bool hasIntrinsicSize(RenderBox*contentRenderer, bool hasIntrinsicWidth, bool hasIntrinsicHeight )
{
    if (hasIntrinsicWidth && hasIntrinsicHeight)
        return true;
    if (hasIntrinsicWidth || hasIntrinsicHeight)
        return contentRenderer && contentRenderer->isRenderOrLegacyRenderSVGRoot();
    return false;
}

LayoutUnit RenderReplaced::computeReplacedLogicalWidth(ShouldComputePreferred shouldComputePreferred) const
{
    if (style().logicalWidth().isSpecified())
        return computeReplacedLogicalWidthRespectingMinMaxWidth(computeReplacedLogicalWidthUsing(style().logicalWidth()), shouldComputePreferred);
    if (style().logicalWidth().isIntrinsic())
        return computeReplacedLogicalWidthRespectingMinMaxWidth(computeReplacedLogicalWidthUsing(style().logicalWidth()), shouldComputePreferred);

    RenderBox* contentRenderer = embeddedContentBox();

    // 10.3.2 Inline, replaced elements: http://www.w3.org/TR/CSS21/visudet.html#inline-replaced-width
    FloatSize intrinsicRatio;
    FloatSize constrainedSize;
    computeIntrinsicSizesConstrainedByTransferredMinMaxSizes(contentRenderer, constrainedSize, intrinsicRatio);

    if (style().logicalWidth().isAuto()) {
        bool computedHeightIsAuto = style().logicalHeight().isAuto();
        bool hasIntrinsicWidth = constrainedSize.width() > 0 || shouldApplySizeOrInlineSizeContainment();
        bool hasIntrinsicHeight = constrainedSize.height() > 0 || shouldApplySizeContainment();

        // For flex or grid items where the logical height has been overriden then we should use that size to compute the replaced width as long as the flex or
        // grid item has an intrinsic size. It is possible (indeed, common) for an SVG graphic to have an intrinsic aspect ratio but not to have an intrinsic
        // width or height. There are also elements with intrinsic sizes but without intrinsic ratio (like an iframe).
        if (auto overridingLogicalHeight = (!intrinsicRatio.isEmpty() && (isFlexItem() || isGridItem()) && hasIntrinsicSize(contentRenderer, hasIntrinsicWidth, hasIntrinsicHeight) ? this->overridingBorderBoxLogicalHeight() : std::nullopt))
            return computeReplacedLogicalWidthRespectingMinMaxWidth(contentBoxLogicalHeight(*overridingLogicalHeight) * intrinsicRatio.aspectRatioDouble(), shouldComputePreferred);

        // If 'height' and 'width' both have computed values of 'auto' and the element also has an intrinsic width, then that intrinsic width is the used value of 'width'.
        if (computedHeightIsAuto && hasIntrinsicWidth)
            return computeReplacedLogicalWidthRespectingMinMaxWidth(constrainedSize.width(), shouldComputePreferred);

        if (!intrinsicRatio.isEmpty()) {
            // If 'height' and 'width' both have computed values of 'auto' and the element has no intrinsic width, but does have an intrinsic height and intrinsic ratio;
            // or if 'width' has a computed value of 'auto', 'height' has some other computed value, and the element does have an intrinsic ratio; then the used value
            // of 'width' is: (used height) * (intrinsic ratio)
            if (!computedHeightIsAuto || (!hasIntrinsicWidth && hasIntrinsicHeight)) {
                auto estimatedUsedWidth = [&] {
                    if (hasIntrinsicWidth)
                        return LayoutUnit(constrainedSize.width());

                    if (shouldComputePreferred == ShouldComputePreferred::ComputePreferred)
                        return computeReplacedLogicalWidthRespectingMinMaxWidth(0_lu, ShouldComputePreferred::ComputePreferred);

                    auto constrainedLogicalWidth = computeConstrainedLogicalWidth();
                    return computeReplacedLogicalWidthRespectingMinMaxWidth(constrainedLogicalWidth, ShouldComputePreferred::ComputeActual);
                }();

                LayoutUnit logicalHeight = computeReplacedLogicalHeight(std::optional<LayoutUnit>(estimatedUsedWidth));
                BoxSizing boxSizing = style().hasAspectRatio() ? style().boxSizingForAspectRatio() : BoxSizing::ContentBox;
                return computeReplacedLogicalWidthRespectingMinMaxWidth(resolveWidthForRatio(borderAndPaddingLogicalHeight(), borderAndPaddingLogicalWidth(), logicalHeight, intrinsicRatio.aspectRatioDouble(), boxSizing), shouldComputePreferred);
            }

            // If 'height' and 'width' both have computed values of 'auto' and the
            // element has an intrinsic ratio but no intrinsic height or width, then
            // the used value of 'width' is undefined in CSS 2.1. However, it is
            // suggested that, if the containing block's width does not itself depend
            // on the replaced element's width, then the used value of 'width' is
            // calculated from the constraint equation used for block-level,
            // non-replaced elements in normal flow.
            if (computedHeightIsAuto && !hasIntrinsicWidth && !hasIntrinsicHeight) {
                bool isFlexItemComputingBaseSize = isFlexItem() && downcast<RenderFlexibleBox>(parent())->isComputingFlexBaseSizes();
                if (shouldComputePreferred == ShouldComputePreferred::ComputePreferred && !isFlexItemComputingBaseSize)
                    return computeReplacedLogicalWidthRespectingMinMaxWidth(0_lu, ShouldComputePreferred::ComputePreferred);
                auto constrainedLogicalWidth = computeConstrainedLogicalWidth();
                return computeReplacedLogicalWidthRespectingMinMaxWidth(constrainedLogicalWidth, ShouldComputePreferred::ComputeActual);
            }
        }

        // Otherwise, if 'width' has a computed value of 'auto', and the element has an intrinsic width, then that intrinsic width is the used value of 'width'.
        if (hasIntrinsicWidth)
            return computeReplacedLogicalWidthRespectingMinMaxWidth(constrainedSize.width(), shouldComputePreferred);

        // Otherwise, if 'width' has a computed value of 'auto', but none of the conditions above are met, then the used value of 'width' becomes 300px. If 300px is too
        // wide to fit the device, UAs should use the width of the largest rectangle that has a 2:1 ratio and fits the device instead.
        // Note: We fall through and instead return intrinsicLogicalWidth() here - to preserve existing WebKit behavior, which might or might not be correct, or desired.
        // Changing this to return cDefaultWidth, will affect lots of test results. Eg. some tests assume that a blank <img> tag (which implies width/height=auto)
        // has no intrinsic size, which is wrong per CSS 2.1, but matches our behavior since a long time.
    }

    return computeReplacedLogicalWidthRespectingMinMaxWidth(intrinsicLogicalWidth(), shouldComputePreferred);
}

LayoutUnit RenderReplaced::computeReplacedLogicalHeight(std::optional<LayoutUnit> estimatedUsedWidth) const
{
    // 10.5 Content height: the 'height' property: http://www.w3.org/TR/CSS21/visudet.html#propdef-height
    if (hasReplacedLogicalHeight())
        return computeReplacedLogicalHeightRespectingMinMaxHeight(computeReplacedLogicalHeightUsing(style().logicalHeight()));

    RenderBox* contentRenderer = embeddedContentBox();

    // 10.6.2 Inline, replaced elements: http://www.w3.org/TR/CSS21/visudet.html#inline-replaced-height
    FloatSize intrinsicRatio;
    FloatSize constrainedSize;
    computeIntrinsicSizesConstrainedByTransferredMinMaxSizes(contentRenderer, constrainedSize, intrinsicRatio);

    bool widthIsAuto = style().logicalWidth().isAuto();
    bool hasIntrinsicHeight = constrainedSize.height() > 0 || shouldApplySizeContainment();
    bool hasIntrinsicWidth = constrainedSize.width() > 0 || shouldApplySizeOrInlineSizeContainment();

    // See computeReplacedLogicalHeight() for a similar check for heights.
    if (auto overridinglogicalWidth = (!intrinsicRatio.isEmpty() && (isFlexItem() || isGridItem()) && hasIntrinsicSize(contentRenderer, hasIntrinsicWidth, hasIntrinsicHeight) ? overridingBorderBoxLogicalWidth() : std::nullopt))
        return computeReplacedLogicalHeightRespectingMinMaxHeight(contentBoxLogicalWidth(*overridinglogicalWidth) * intrinsicRatio.transposedSize().aspectRatioDouble());

    // If 'height' and 'width' both have computed values of 'auto' and the element also has an intrinsic height, then that intrinsic height is the used value of 'height'.
    if (widthIsAuto && hasIntrinsicHeight)
        return computeReplacedLogicalHeightRespectingMinMaxHeight(constrainedSize.height());

    // Otherwise, if 'height' has a computed value of 'auto', and the element has an intrinsic ratio then the used value of 'height' is:
    // (used width) / (intrinsic ratio)
    if (!intrinsicRatio.isEmpty()) {
        LayoutUnit usedWidth = estimatedUsedWidth ? estimatedUsedWidth.value() : contentBoxLogicalWidth();
        BoxSizing boxSizing = BoxSizing::ContentBox;
        if (style().hasAspectRatio())
            boxSizing = style().boxSizingForAspectRatio();
        return computeReplacedLogicalHeightRespectingMinMaxHeight(resolveHeightForRatio(borderAndPaddingLogicalWidth(), borderAndPaddingLogicalHeight(), usedWidth, intrinsicRatio.transposedSize().aspectRatioDouble(), boxSizing));
    }

    // Otherwise, if 'height' has a computed value of 'auto', and the element has an intrinsic height, then that intrinsic height is the used value of 'height'.
    if (hasIntrinsicHeight)
        return computeReplacedLogicalHeightRespectingMinMaxHeight(constrainedSize.height());

    // Otherwise, if 'height' has a computed value of 'auto', but none of the conditions above are met, then the used value of 'height' must be set to the height
    // of the largest rectangle that has a 2:1 ratio, has a height not greater than 150px, and has a width not greater than the device width.
    return computeReplacedLogicalHeightRespectingMinMaxHeight(intrinsicLogicalHeight());
}

void RenderReplaced::computeIntrinsicLogicalWidths(LayoutUnit& minLogicalWidth, LayoutUnit& maxLogicalWidth) const
{
    minLogicalWidth = maxLogicalWidth = intrinsicLogicalWidth();
}

void RenderReplaced::computePreferredLogicalWidths()
{
    ASSERT(needsPreferredLogicalWidthsUpdate());

    // We cannot resolve any percent logical width here as the available logical
    // width may not be set on our containing block.
    if (style().logicalWidth().isPercentOrCalculated())
        computeAspectRatioAdjustedIntrinsicLogicalWidths(m_minPreferredLogicalWidth, m_maxPreferredLogicalWidth);
    else
        m_minPreferredLogicalWidth = m_maxPreferredLogicalWidth = computeReplacedLogicalWidth(ShouldComputePreferred::ComputePreferred);

    bool ignoreMinMaxSizes = shouldIgnoreLogicalMinMaxWidthSizes();
    const RenderStyle& styleToUse = style();
    if (styleToUse.logicalWidth().isPercentOrCalculated() || styleToUse.logicalMaxWidth().isPercentOrCalculated())
        m_minPreferredLogicalWidth = 0;

    if (auto fixedLogicalMinWidth = styleToUse.logicalMinWidth().tryFixed(); !ignoreMinMaxSizes && fixedLogicalMinWidth && fixedLogicalMinWidth->value > 0) {
        m_maxPreferredLogicalWidth = std::max(m_maxPreferredLogicalWidth, adjustContentBoxLogicalWidthForBoxSizing(*fixedLogicalMinWidth));
        m_minPreferredLogicalWidth = std::max(m_minPreferredLogicalWidth, adjustContentBoxLogicalWidthForBoxSizing(*fixedLogicalMinWidth));
    }
    
    if (auto fixedLogicalMaxWidth = styleToUse.logicalMaxWidth().tryFixed(); !ignoreMinMaxSizes && fixedLogicalMaxWidth) {
        m_maxPreferredLogicalWidth = std::min(m_maxPreferredLogicalWidth, adjustContentBoxLogicalWidthForBoxSizing(*fixedLogicalMaxWidth));
        m_minPreferredLogicalWidth = std::min(m_minPreferredLogicalWidth, adjustContentBoxLogicalWidthForBoxSizing(*fixedLogicalMaxWidth));
    }

    LayoutUnit borderAndPadding = borderAndPaddingLogicalWidth();
    m_minPreferredLogicalWidth += borderAndPadding;
    m_maxPreferredLogicalWidth += borderAndPadding;

    clearNeedsPreferredWidthsUpdate();
}

VisiblePosition RenderReplaced::positionForPoint(const LayoutPoint& point, HitTestSource source, const RenderFragmentContainer* fragment)
{
    auto [top, bottom] = [&]() -> std::pair<float, float> {
        if (auto run = InlineIterator::boxFor(*this)) {
            auto lineBox = run->lineBox();
            auto lineContentTop = LayoutUnit { std::min(previousLineBoxContentBottomOrBorderAndPadding(*lineBox), lineBox->contentLogicalTop()) };
            return std::make_pair(lineContentTop, LineSelection::logicalBottom(*lineBox));
        }
        return std::make_pair(logicalTop(), logicalBottom());
    }();

    LayoutUnit blockDirectionPosition = isHorizontalWritingMode() ? point.y() + y() : point.x() + x();
    LayoutUnit lineDirectionPosition = isHorizontalWritingMode() ? point.x() + x() : point.y() + y();
    
    if (blockDirectionPosition < top)
        return createVisiblePosition(caretMinOffset(), Affinity::Downstream); // coordinates are above
    
    if (blockDirectionPosition >= bottom)
        return createVisiblePosition(caretMaxOffset(), Affinity::Downstream); // coordinates are below
    
    if (element()) {
        if (lineDirectionPosition <= logicalLeft() + (logicalWidth() / 2))
            return createVisiblePosition(0, Affinity::Downstream);
        return createVisiblePosition(1, Affinity::Downstream);
    }

    return RenderBox::positionForPoint(point, source, fragment);
}

LayoutRect RenderReplaced::selectionRectForRepaint(const RenderLayerModelObject* repaintContainer, bool clipToVisibleContent)
{
    ASSERT(!needsLayout());

    if (!isSelected())
        return LayoutRect();
    
    LayoutRect rect = localSelectionRect();
    if (clipToVisibleContent)
        return computeRectForRepaint(rect, repaintContainer);
    return localToContainerQuad(FloatRect(rect), repaintContainer).enclosingBoundingBox();
}

LayoutRect RenderReplaced::localSelectionRect(bool checkWhetherSelected) const
{
    if (checkWhetherSelected && !isSelected())
        return LayoutRect();

    return LayoutRect(LayoutPoint(), size());
}

bool RenderReplaced::isSelected() const
{
    return isHighlighted(selectionState(), view().selection());
}

bool RenderReplaced::isHighlighted(HighlightState state, const RenderHighlight& rangeData) const
{
    if (state == HighlightState::None)
        return false;
    if (state == HighlightState::Inside)
        return true;

    auto selectionStart = rangeData.startOffset();
    auto selectionEnd = rangeData.endOffset();
    if (state == HighlightState::Start)
        return !selectionStart;

    unsigned end = element()->hasChildNodes() ? element()->countChildNodes() : 1;
    if (state == HighlightState::End)
        return selectionEnd == end;
    if (state == HighlightState::Both)
        return !selectionStart && selectionEnd == end;
    ASSERT_NOT_REACHED();
    return false;
}

auto RenderReplaced::localRectsForRepaint(RepaintOutlineBounds repaintOutlineBounds) const -> RepaintRects
{
    if (isInsideEntirelyHiddenLayer())
        return { };

    // The selectionRect can project outside of the overflowRect, so take their union
    // for repainting to avoid selection painting glitches.
    auto overflowRect = unionRect(localSelectionRect(false), visualOverflowRect());

    // FIXME: layoutDelta needs to be applied in parts before/after transforms and
    // repaint containers. https://bugs.webkit.org/show_bug.cgi?id=23308
    overflowRect.move(view().frameView().layoutContext().layoutDelta());

    auto rects = RepaintRects { overflowRect };
    if (repaintOutlineBounds == RepaintOutlineBounds::Yes)
        rects.outlineBoundsRect = localOutlineBoundsRepaintRect();

    return rects;
}

bool RenderReplaced::isContentLikelyVisibleInViewport()
{
    if (!isVisibleIgnoringGeometry())
        return false;

    auto& frameView = view().frameView();
    auto visibleRect = LayoutRect(frameView.windowToContents(frameView.windowClipRect()));
    auto contentRect = computeRectForRepaint(replacedContentRect(), nullptr);

    // Content rectangle may be empty because it is intrinsically sized and the content has not loaded yet.
    if (contentRect.isEmpty() && (style().logicalWidth().isAuto() || style().logicalHeight().isAuto()))
        return visibleRect.contains(contentRect.location());

    return visibleRect.intersects(contentRect);
}

bool RenderReplaced::shouldInvalidatePreferredWidths() const
{
    // If the height is a percentage and the width is auto, then the containingBlocks's height changing can cause this node to change it's preferred width because it maintains aspect ratio.
    return (hasRelativeLogicalHeight() || (isGridItem() && hasStretchedLogicalHeight())) && style().logicalWidth().isAuto();
}

LayoutSize RenderReplaced::intrinsicSize() const
{
    if (!view().frameView().layoutContext().isInRenderTreeLayout()) {
        // 'contain' removes the natural aspect ratio / width / height only for the purposes of sizing and layout of the box.
        return m_intrinsicSize;
    }

    auto size = m_intrinsicSize;
    auto zoomValue = style().usedZoom();
    if (isHorizontalWritingMode() ? shouldApplySizeOrInlineSizeContainment() : shouldApplySizeContainment())
        size.setWidth(explicitIntrinsicInnerWidth().value_or(0_lu) * zoomValue);
    if (isHorizontalWritingMode() ? shouldApplySizeContainment() : shouldApplySizeOrInlineSizeContainment())
        size.setHeight(explicitIntrinsicInnerHeight().value_or(0_lu) * zoomValue);
    return size;
}

void RenderReplaced::layoutShadowContent(const LayoutSize& oldSize)
{
    for (auto& renderBox : childrenOfType<RenderBox>(*this)) {
        auto newSize = contentBoxRect().size();

        if (is<RenderImage>(this)) {
            bool childNeedsLayout = renderBox.needsLayout();
            // If the region chain has changed we also need to relayout the children to update the region box info.
            // FIXME: We can do better once we compute region box info for RenderReplaced, not only for RenderBlock.
            if (CheckedPtr fragmentedFlow = enclosingFragmentedFlow(); fragmentedFlow && !childNeedsLayout) {
                if (fragmentedFlow->pageLogicalSizeChanged())
                    childNeedsLayout = true;
            }

            if (newSize == oldSize && !childNeedsLayout)
                continue;
        }

        // When calling layout() on a child node, a parent must either push a LayoutStateMaintainer, or
        // instantiate LayoutStateDisabler. Since using a LayoutStateMaintainer is slightly more efficient,
        // and this method might be called many times per second during video playback, use a LayoutStateMaintainer:
        LayoutStateMaintainer statePusher(*this, locationOffset(), isTransformed() || hasReflection() || writingMode().isBlockFlipped());
        renderBox.setLocation(LayoutPoint(borderLeft(), borderTop()) + LayoutSize(paddingLeft(), paddingTop()));
        renderBox.mutableStyle().setHeight(Style::PreferredSize::Fixed { newSize.height() });
        renderBox.mutableStyle().setWidth(Style::PreferredSize::Fixed { newSize.width() });
        renderBox.setNeedsLayout(MarkOnlyThis);
        renderBox.layout();
    }

    clearChildNeedsLayout();
}

FloatSize RenderReplaced::intrinsicRatio() const
{
    FloatSize intrinsicRatio;
    FloatSize constrainedSize;
    computeAspectRatioInformationForRenderBox(embeddedContentBox(), constrainedSize, intrinsicRatio);
    return intrinsicRatio;
}

}
