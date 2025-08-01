/*
 * Copyright (C) 2014-2021 Apple Inc. All rights reserved.
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
#include "DebugPageOverlays.h"

#include "ColorHash.h"
#include "Cursor.h"
#include "ElementIterator.h"
#include "FloatRoundedRect.h"
#include "Gradient.h"
#include "GraphicsContext.h"
#include "HitTestResult.h"
#include "InteractionRegion.h"
#include "LocalFrameView.h"
#include "Page.h"
#include "PageOverlay.h"
#include "PageOverlayController.h"
#include "PathUtilities.h"
#include "PlatformMouseEvent.h"
#include "Region.h"
#include "RemoteFrame.h"
#include "RenderLayer.h"
#include "RenderLayerBacking.h"
#include "RenderObjectInlines.h"
#include "RenderView.h"
#include "ScrollingCoordinator.h"
#include "Settings.h"
#include <wtf/SortedArrayMap.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

DebugPageOverlays* DebugPageOverlays::sharedDebugOverlays;

class RegionOverlay : public RefCounted<RegionOverlay>, public PageOverlayClient {
public:
    static Ref<RegionOverlay> create(Page&, DebugPageOverlays::RegionType);
    virtual ~RegionOverlay();

    void recomputeRegion();
    PageOverlay& overlay() { return *m_overlay; }
    Ref<PageOverlay> protectedOverlay() { return *m_overlay; }

    void setRegionChanged() { m_regionChanged = true; }

    virtual bool shouldPaintOverlayIntoLayer() const { return true; }

protected:
    RegionOverlay(Page&, Color);

private:
    void willMoveToPage(PageOverlay&, Page*) final;
    void didMoveToPage(PageOverlay&, Page*) final;
    void drawRect(PageOverlay&, GraphicsContext&, const IntRect& dirtyRect) override;
    bool mouseEvent(PageOverlay&, const PlatformMouseEvent&) override;
    void didScrollFrame(PageOverlay&, LocalFrame&) override;

protected:
    // Returns true if the region changed.
    virtual bool updateRegion() = 0;
    void drawRegion(GraphicsContext&, const Region&, const Color&, const IntRect& dirtyRect);
    
    WeakPtr<Page> m_page;
    RefPtr<PageOverlay> m_overlay;
    std::unique_ptr<Region> m_region;
    Color m_color;
    bool m_regionChanged { true };
};

#if COMPILER(CLANG)
#pragma mark - MouseWheelRegionOverlay
#endif

class MouseWheelRegionOverlay final : public RegionOverlay {
public:
    static Ref<MouseWheelRegionOverlay> create(Page& page)
    {
        return adoptRef(*new MouseWheelRegionOverlay(page));
    }

private:
    explicit MouseWheelRegionOverlay(Page& page)
        : RegionOverlay(page, SRGBA<uint8_t> { 128, 0, 0, 102 })
    {
    }

    bool updateRegion() override;
};

bool MouseWheelRegionOverlay::updateRegion()
{
    RefPtr page = m_page.get();
    if (!page)
        return false;
#if ENABLE(WHEEL_EVENT_REGIONS)
    // Wheel event regions are painted via RenderLayerBacking::paintDebugOverlays().
    return false;
#else
    auto region = makeUnique<Region>();
    
    for (RefPtr frame = page->mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        if (!localFrame->view() || !localFrame->document())
            continue;

        Ref document = *localFrame->document();
        auto frameRegion = document->absoluteRegionForEventTargets(document->wheelEventTargets());
        frameRegion.first.translate(toIntSize(localFrame->protectedView()->contentsToRootView(IntPoint())));
        region->unite(frameRegion.first);
    }
    
    region->translate(m_overlay->viewToOverlayOffset());

    bool regionChanged = !m_region || !(*m_region == *region);
    m_region = WTFMove(region);
    return regionChanged;
#endif
}

#if COMPILER(CLANG)
#pragma mark - NonFastScrollableRegionOverlay
#endif

class NonFastScrollableRegionOverlay final : public RegionOverlay {
public:
    static Ref<NonFastScrollableRegionOverlay> create(Page& page)
    {
        return adoptRef(*new NonFastScrollableRegionOverlay(page));
    }

private:
    explicit NonFastScrollableRegionOverlay(Page& page)
        : RegionOverlay(page, Color::orange.colorWithAlphaByte(102))
    {
    }

    bool updateRegion() override;
    void drawRect(PageOverlay&, GraphicsContext&, const IntRect& dirtyRect) final;
    
    EventTrackingRegions m_eventTrackingRegions;
};

bool NonFastScrollableRegionOverlay::updateRegion()
{
    RefPtr page = m_page.get();
    if (!page)
        return false;
    bool regionChanged = false;

    if (RefPtr scrollingCoordinator = m_page->scrollingCoordinator()) {
        EventTrackingRegions eventTrackingRegions = scrollingCoordinator->absoluteEventTrackingRegions();

        if (eventTrackingRegions != m_eventTrackingRegions) {
            m_eventTrackingRegions = eventTrackingRegions;
            regionChanged = true;
        }
    }

    return regionChanged;
}

static void drawRightAlignedText(const String& text, GraphicsContext& context, const FontCascade& font, const FloatPoint& boxLocation)
{
    float textGap = 10;
    float textBaselineFromTop = 14;

    TextRun textRun = TextRun(text);
    float textWidth = font.width(textRun);
    context.setFillColor(Color::black);
    context.drawText(font, textRun, boxLocation + FloatSize(-(textWidth + textGap), textBaselineFromTop));
}

void NonFastScrollableRegionOverlay::drawRect(PageOverlay& pageOverlay, GraphicsContext& context, const IntRect&)
{
    static constexpr std::pair<EventTrackingRegions::EventType, SRGBA<uint8_t>> colorMappings[] = {
        { EventTrackingRegions::EventType::Mousedown, { 80, 245, 80, 50 } },
        { EventTrackingRegions::EventType::Mousemove, { 245, 245, 80, 50 } },
        { EventTrackingRegions::EventType::Mouseup, { 80, 245, 176, 50 } },
        { EventTrackingRegions::EventType::Touchend, { 191, 63, 127, 50 } },
        { EventTrackingRegions::EventType::Touchforcechange, { 63, 63, 191, 50 } },
        { EventTrackingRegions::EventType::Touchmove, { 80, 204, 245, 50 } },
        { EventTrackingRegions::EventType::Touchstart, { 191, 191, 63, 50 } },
        { EventTrackingRegions::EventType::Wheel, { 255, 128, 0, 50 } },
    };
    constexpr SortedArrayMap colors { colorMappings };
    constexpr auto defaultColor = Color::black.colorWithAlphaByte(64);

    IntRect bounds = pageOverlay.bounds();
    
    context.clearRect(bounds);
    
    FloatRect legendRect = { bounds.maxX() - 30.0f, 10, 20, 20 };
    
    FontCascadeDescription fontDescription;
    fontDescription.setOneFamily("Helvetica"_s);
    fontDescription.setSpecifiedSize(12);
    fontDescription.setComputedSize(12);
    fontDescription.setWeight(FontSelectionValue(500));
    FontCascade font(WTFMove(fontDescription));
    font.update(nullptr);

    auto drawLegend = [&] (const Color& color, ASCIILiteral text) {
        context.setFillColor(color);
        context.fillRect(legendRect);
        drawRightAlignedText(text, context, font, legendRect.location());
        legendRect.move(0, 30);
    };

#if ENABLE(TOUCH_EVENTS)
    auto drawEventLegend = [&](EventTrackingRegions::EventType eventType) {
        drawLegend(colors.get(eventType), EventTrackingRegions::eventName(eventType));
    };
    drawEventLegend(EventTrackingRegions::EventType::Touchstart);
    drawEventLegend(EventTrackingRegions::EventType::Touchmove);
    drawEventLegend(EventTrackingRegions::EventType::Touchend);
    drawEventLegend(EventTrackingRegions::EventType::Touchforcechange);
    drawLegend(m_color, "passive listeners"_s);
    drawEventLegend(EventTrackingRegions::EventType::Mousedown);
    drawEventLegend(EventTrackingRegions::EventType::Mousemove);
    drawEventLegend(EventTrackingRegions::EventType::Mouseup);
#else
    // On desktop platforms, the "wheel" region includes the non-fast scrollable region.
    drawLegend(colors.get(EventTrackingRegions::EventType::Wheel), "non-fast region"_s);
#endif

    for (auto& region : m_eventTrackingRegions.eventSpecificSynchronousDispatchRegions)
        drawRegion(context, region.value, colors.get(region.key, defaultColor), bounds);
    drawRegion(context, m_eventTrackingRegions.asynchronousDispatchRegion, m_color, bounds);
}

#if COMPILER(CLANG)
#pragma mark - InteractionRegionOverlay
#endif

class InteractionRegionOverlay final : public RegionOverlay {
public:
    static Ref<InteractionRegionOverlay> create(Page& page)
    {
        return adoptRef(*new InteractionRegionOverlay(page));
    }

private:
    explicit InteractionRegionOverlay(Page& page)
        : RegionOverlay(page, Color::green.colorWithAlphaByte(102))
    {
    }

    bool updateRegion() final;
    void drawRect(PageOverlay&, GraphicsContext&, const IntRect& dirtyRect) final;

    void drawSettings(GraphicsContext&);

    bool mouseEvent(PageOverlay&, const PlatformMouseEvent&) final;

    FloatRect rectForSettingAtIndex(unsigned) const;
    bool valueForSetting(ASCIILiteral) const;

    std::optional<std::pair<RenderLayer&, GraphicsLayer&>> activeLayer() const;
    std::optional<InteractionRegion> activeRegion() const;

    bool shouldPaintOverlayIntoLayer() const override { return valueForSetting("regions"_s); }

    struct Setting {
        ASCIILiteral key;
        ASCIILiteral name;
        bool value { false };
    };    

    FixedVector<Setting> m_settings {
        { "constrain"_s, "Constrain to Regions"_s, true },
        { "clip"_s, "Clip to Regions"_s, true },
        { "wash"_s, "Draw Wash"_s, false },
        { "contextualSize"_s, "Contextual Size"_s, true },
        { "cursor"_s, "Show Cursor"_s, true },
        { "hover"_s, "CSS Hover"_s, false },
        { "regions"_s, "Show Regions"_s, false }
    };

    IntPoint m_mouseLocationInContentCoordinates;
};

bool InteractionRegionOverlay::updateRegion()
{
    m_overlay->setNeedsDisplay();
    return true;
}

static Vector<Path> pathsForRect(const FloatRect& rect, float borderRadius)
{
    static constexpr float radius = 4;

    Vector<FloatRect> rects;
    rects.append(rect);
    return PathUtilities::pathsWithShrinkWrappedRects(rects, std::max(borderRadius, radius));
}

std::optional<std::pair<RenderLayer&, GraphicsLayer&>> InteractionRegionOverlay::activeLayer() const
{
    RefPtr page = m_page.get();
    if (!page)
        return std::nullopt;
    constexpr OptionSet<HitTestRequest::Type> hitType {
        HitTestRequest::Type::ReadOnly,
        HitTestRequest::Type::Active,
        HitTestRequest::Type::AllowChildFrameContent
    };
    HitTestResult result(m_mouseLocationInContentCoordinates);
    RefPtr localTopDocument = page->localTopDocument();
    if (!localTopDocument)
        return std::nullopt;

    localTopDocument->hitTest(hitType, result);

    RefPtr hitNode = result.innerNode();
    if (!hitNode || !hitNode->renderer())
        return std::nullopt;

    CheckedPtr rendererLayer = hitNode->renderer()->enclosingLayer();
    if (!rendererLayer)
        return std::nullopt;

    CheckedPtr layer = rendererLayer->enclosingCompositingLayerForRepaint().layer;
    if (!layer)
        return std::nullopt;

    auto* backing = layer->backing();
    if (!backing)
        return std::nullopt;

    RefPtr graphicsLayer = backing->graphicsLayer();
    if (!graphicsLayer)
        return std::nullopt;

    return { { *layer, *graphicsLayer } };
}

std::optional<InteractionRegion> InteractionRegionOverlay::activeRegion() const
{
#if ENABLE(INTERACTION_REGIONS_IN_EVENT_REGION)
    RefPtr page = m_page.get();
    if (!page)
        return std::nullopt;
    auto layerPair = activeLayer();
    if (!layerPair)
        return std::nullopt;
    auto& [layer, graphicsLayer] = *layerPair;

    std::optional<InteractionRegion> hitRegion;
    IntRect hitRectInOverlayCoordinates;
    float hitRegionArea = 0;

    RefPtr localMainFrame = page->localMainFrame();
    if (!localMainFrame)
        return std::nullopt;

    auto regions = graphicsLayer.eventRegion().interactionRegions();
    for (const auto& region : regions) {
        float area = 0;
        FloatRect boundingRect;

        auto rect = region.rectInLayerCoordinates;
        rect.move(roundedIntSize(graphicsLayer.offsetFromRenderer()));
        IntRect rectInOverlayCoordinates = layer.renderer().localToAbsoluteQuad(FloatRect { rect }).enclosingBoundingBox();
        if (boundingRect.isEmpty())
            boundingRect = rectInOverlayCoordinates;
        else
            boundingRect.unite(rectInOverlayCoordinates);
        area += rectInOverlayCoordinates.area();

        if (!boundingRect.contains(m_mouseLocationInContentCoordinates))
            continue;

        auto paths = pathsForRect(rectInOverlayCoordinates, region.cornerRadius);
        bool didHitRegion = false;
        for (const auto& path : paths) {
            if (path.contains(m_mouseLocationInContentCoordinates)) {
                didHitRegion = true;
                break;
            }
        }
        
        if (!didHitRegion)
            continue;

        if (area > localMainFrame->view()->layoutSize().area() / 2)
            continue;

        if (didHitRegion && (!hitRegion || area < hitRegionArea)) {
            hitRegion = region;
            hitRegionArea = area;
            hitRectInOverlayCoordinates = rectInOverlayCoordinates;
        }
    }
    
    if (hitRegion) {
        if (hitRegion->type == InteractionRegion::Type::Occlusion)
            return std::nullopt;
        
        hitRegion->rectInLayerCoordinates = hitRectInOverlayCoordinates;
    }

    return hitRegion;
#else
    return std::nullopt;
#endif
}

static void drawCheckbox(const String& text, GraphicsContext& context, const FontCascade& font, const FloatRect& box, bool state)
{
    static constexpr float lineHeight = 14;
    static constexpr float checkboxVerticalPadding = 2;
    static constexpr float textHorizontalPadding = 4;

    FloatRect checkboxRect { box.location() + FloatSize { 0, checkboxVerticalPadding }, FloatSize { lineHeight, lineHeight } };

    TextRun textRun = TextRun(text);
    context.setFillColor(Color::black);
    context.drawText(font, textRun, box.location() + FloatSize { checkboxRect.width() + textHorizontalPadding, lineHeight });

    Path checkboxPath;
    checkboxPath.addRoundedRect(FloatRoundedRect { checkboxRect, FloatRoundedRect::Radii { 3 } });

    if (state) {
        context.setFillColor(Color::darkGray);
        context.fillPath(checkboxPath);
    }

    context.setStrokeColor(Color::black.colorWithAlphaByte(127));
    context.setStrokeThickness(1);
    context.strokePath(checkboxPath);
}

FloatRect InteractionRegionOverlay::rectForSettingAtIndex(unsigned index) const
{
    RefPtr mainFrameView = m_page->mainFrame().virtualView();
    if (!mainFrameView)
        return FloatRect();
    auto viewSize = mainFrameView->layoutSize();
    static constexpr float settingsWidth = 150;
    static constexpr float rowHeight = 16;
    return {
        FloatPoint { viewSize.width() - settingsWidth - 14, 10 }
            + FloatSize { 4, rowHeight * index + 2 },
        FloatSize { settingsWidth, rowHeight }
    };
}

bool InteractionRegionOverlay::valueForSetting(ASCIILiteral name) const
{
    for (const auto& setting : m_settings) {
        if (name == setting.key)
            return setting.value;
    }

    ASSERT_NOT_REACHED();
    return false;
}

void InteractionRegionOverlay::drawSettings(GraphicsContext& context)
{
    GraphicsContextStateSaver stateSaver(context);

    FloatRect rect = rectForSettingAtIndex(0);
    for (unsigned i = 1; i < m_settings.size(); i++)
        rect.unite(rectForSettingAtIndex(i));

    rect.expand(FloatBoxExtent { 4.0f, 4.0f, 4.0f, 4.0f });

    {
        GraphicsContextStateSaver stateSaver(context);
        context.setDropShadow({ { }, 5, Color(Color::black).colorWithAlpha(0.5), ShadowRadiusMode::Default });
        context.fillRoundedRect(FloatRoundedRect { rect, FloatRoundedRect::Radii { 6 } }, Color(Color::white).colorWithAlpha(0.85));
    }

    FontCascadeDescription fontDescription;
    fontDescription.setOneFamily("Helvetica"_s);
    fontDescription.setSpecifiedSize(12);
    fontDescription.setComputedSize(12);
    fontDescription.setWeight(FontSelectionValue(500));
    FontCascade font(WTFMove(fontDescription));
    font.update(nullptr);

    for (unsigned i = 0; i < m_settings.size(); i++) {
        const auto& setting = m_settings[i];
        drawCheckbox(setting.name, context, font, rectForSettingAtIndex(i), setting.value);
    }
}

void InteractionRegionOverlay::drawRect(PageOverlay&, GraphicsContext& context, const IntRect& dirtyRect)
{
    GraphicsContextStateSaver stateSaver(context);
    
    context.clearRect(dirtyRect);

    auto region = activeRegion();

    if (region || !valueForSetting("constrain"_s)) {
        auto gradientData = [&] (float radius) {
            Gradient::RadialData gradientData;
            gradientData.point0 = m_mouseLocationInContentCoordinates;
            gradientData.point1 = m_mouseLocationInContentCoordinates;
            gradientData.startRadius = 0;
            gradientData.endRadius = radius;
            gradientData.aspectRatio = 1;
            return gradientData;
        };

        auto makeGradient = [&] (Gradient::RadialData gradientData) {
            auto gradient = Gradient::create(WTFMove(gradientData), { ColorInterpolationMethod::SRGB { }, AlphaPremultiplication::Unpremultiplied });
            if (region && valueForSetting("wash"_s) && valueForSetting("clip"_s)) {
                gradient->addColorStop({ 0.1, Color(Color::white).colorWithAlpha(0.5) });
                gradient->addColorStop({ 1, Color(Color::white).colorWithAlpha(0.1) });
            } else if (!valueForSetting("clip"_s) || !valueForSetting("constrain"_s)) {
                gradient->addColorStop({ 0.1, Color(Color::white).colorWithAlpha(0.2) });
                gradient->addColorStop({ 1, Color(Color::white).colorWithAlpha(0) });
            } else {
                gradient->addColorStop({ 0.1, Color(Color::white).colorWithAlpha(0.5) });
                gradient->addColorStop({ 1, Color(Color::white).colorWithAlpha(0) });
            }

            return gradient;
        };
        
        constexpr float defaultRadius = 50;
        bool shouldClip = valueForSetting("clip"_s);
        Vector<Path> clipPaths;

        if (shouldClip) {
            const auto rectInLayerCoordinates = region->rectInLayerCoordinates;

            if (auto clipPath = region->clipPath) {
                Path existingClip = *clipPath;
                AffineTransform transform;

                transform.translate(rectInLayerCoordinates.location());
                if (RefPtr page = m_page.get())
                    transform.scale(page->pageScaleFactor());

                existingClip.transform(transform);
                clipPaths.append(existingClip);
            } else {
                auto scaleFactor = 1.f;
                if (RefPtr page = m_page.get())
                    scaleFactor = page->pageScaleFactor();

                if (region->useContinuousCorners) {
                    Path path;
                    path.addContinuousRoundedRect(rectInLayerCoordinates, region->cornerRadius * scaleFactor);
                    clipPaths.append(path);
                } else
                    clipPaths = pathsForRect(rectInLayerCoordinates, region->cornerRadius * scaleFactor);
            }
        }

        bool shouldUseBackdropGradient = !shouldClip || !region || (!valueForSetting("wash"_s) && valueForSetting("clip"_s));

        if (shouldUseBackdropGradient) {
            if (shouldClip) {
                for (const auto& path : clipPaths) {
                    float radius = valueForSetting("contextualSize"_s) ? 1.5 * path.boundingRect().size().minDimension() : defaultRadius;
                    auto backdropGradient = Gradient::create(gradientData(radius * 1.5), { ColorInterpolationMethod::SRGB { }, AlphaPremultiplication::Unpremultiplied });
                    backdropGradient->addColorStop({ 0.1, Color(Color::black).colorWithAlpha(0.2) });
                    backdropGradient->addColorStop({ 1, Color(Color::black).colorWithAlpha(0) });

                    context.setFillGradient(WTFMove(backdropGradient));
                    context.fillPath(path);
                }
            } else {
                auto backdropGradient = Gradient::create(gradientData(defaultRadius * 2), { ColorInterpolationMethod::SRGB { }, AlphaPremultiplication::Unpremultiplied });
                backdropGradient->addColorStop({ 0.1, Color(Color::black).colorWithAlpha(0.2) });
                backdropGradient->addColorStop({ 1, Color(Color::black).colorWithAlpha(0) });

                context.setFillGradient(WTFMove(backdropGradient));
                context.fillRect(dirtyRect);    
            }
        }

        if (shouldClip) {
            for (const auto& path : clipPaths) {
                float radius = valueForSetting("contextualSize"_s) ? 1.5 * path.boundingRect().size().minDimension() : defaultRadius;
                context.setFillGradient(makeGradient(gradientData(radius)));
                context.fillPath(path);
            }
        } else {
            context.setFillGradient(makeGradient(gradientData(defaultRadius)));
            context.fillRect(dirtyRect);
        }

#if ENABLE(INTERACTION_REGION_TEXT_CONTENT)
        FontCascadeDescription fontDescription;
        fontDescription.setOneFamily("Helvetica"_s);
        fontDescription.setSpecifiedSize(10);
        fontDescription.setComputedSize(10);
        fontDescription.setWeight(FontSelectionValue(500));
        FontCascade font(WTFMove(fontDescription));
        font.update(nullptr);

        TextRun textRun = TextRun(region->text);
        context.setFillColor(Color::black);
        context.drawText(font, textRun, region->rectInLayerCoordinates.location());
#endif
    }

    stateSaver.restore();

    drawSettings(context);
}

bool InteractionRegionOverlay::mouseEvent(PageOverlay& overlay, const PlatformMouseEvent& event)
{
    RefPtr page = m_page.get();
    if (!page)
        return false;
    RefPtr localMainFrame = page->localMainFrame();
    if (!localMainFrame)
        return false;
    RefPtr mainFrameView = localMainFrame->view();

    std::optional<Cursor> cursorToSet;

    if (!valueForSetting("cursor"_s))
        cursorToSet = noneCursor();
    else if (!valueForSetting("hover"_s))
        cursorToSet = pointerCursor();

    auto eventInContentsCoordinates = mainFrameView->windowToContents(event.position());
    for (unsigned i = 0; i < m_settings.size(); i++) {
        if (!rectForSettingAtIndex(i).contains(eventInContentsCoordinates))
            continue;
        cursorToSet = handCursor();
        if (event.button() == MouseButton::Left && event.type() == PlatformEvent::Type::MousePressed) {
            m_settings[i].value = !m_settings[i].value;
            page->forceRepaintAllFrames();
            return true;
        }
    }

    if (cursorToSet)
        mainFrameView->setCursor(*cursorToSet);

    m_mouseLocationInContentCoordinates = eventInContentsCoordinates;
    overlay.setNeedsDisplay();

    if (event.type() == PlatformEvent::Type::MouseMoved && !event.buttons() && !valueForSetting("hover"_s))
        return true;

    return false;
}

#if COMPILER(CLANG)
#pragma mark - SiteIsolationOverlay
#endif

class SiteIsolationOverlay final : public RegionOverlay {
public:
    static Ref<SiteIsolationOverlay> create(Page& page)
    {
        return adoptRef(*new SiteIsolationOverlay(page));
    }

private:
    explicit SiteIsolationOverlay(Page& page)
        : RegionOverlay(page, Color::green.colorWithAlphaByte(102))
    {
    }

    bool updateRegion() final;
    void drawRect(PageOverlay&, GraphicsContext&, const IntRect& dirtyRect) final;

    bool mouseEvent(PageOverlay&, const PlatformMouseEvent&) final;
};

bool SiteIsolationOverlay::updateRegion()
{
    m_overlay->setNeedsDisplay();
    return true;
}

void SiteIsolationOverlay::drawRect(PageOverlay&, GraphicsContext& context, const IntRect&)
{
    RefPtr page = m_page.get();
    if (!page)
        return;
    GraphicsContextStateSaver stateSaver(context);

    FontCascadeDescription fontDescription;
    fontDescription.setOneFamily("Helvetica"_s);
    fontDescription.setSpecifiedSize(12);
    fontDescription.setComputedSize(12);
    fontDescription.setWeight(FontSelectionValue(500));
    FontCascade font(WTFMove(fontDescription));
    font.update(nullptr);

    for (RefPtr frame = page->mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (!frame->virtualView())
            continue;
        auto frameView = frame->virtualView();
        auto debugStr = makeString(is<RemoteFrame>(frame) ? "remote("_s : "local("_s, frame->frameID().toUInt64(), ')');
        TextRun textRun = TextRun(debugStr);
        context.setFillColor(Color::black);

        context.drawText(font, textRun, FloatPoint { static_cast<float>(frameView->x()), static_cast<float>(frameView->y() + 12) });
    }
}

bool SiteIsolationOverlay::mouseEvent(PageOverlay& , const PlatformMouseEvent&)
{
    return false;
}

#if COMPILER(CLANG)
#pragma mark - RegionOverlay
#endif

Ref<RegionOverlay> RegionOverlay::create(Page& page, DebugPageOverlays::RegionType regionType)
{
    switch (regionType) {
    case DebugPageOverlays::RegionType::WheelEventHandlers:
        return MouseWheelRegionOverlay::create(page);
    case DebugPageOverlays::RegionType::NonFastScrollableRegion:
        return NonFastScrollableRegionOverlay::create(page);
    case DebugPageOverlays::RegionType::InteractionRegion:
        return InteractionRegionOverlay::create(page);
    case DebugPageOverlays::RegionType::SiteIsolationRegion:
        return SiteIsolationOverlay::create(page);
    }
    ASSERT_NOT_REACHED();
    return MouseWheelRegionOverlay::create(page);
}

RegionOverlay::RegionOverlay(Page& page, Color regionColor)
    : m_page(page)
    , m_overlay(PageOverlay::create(*this, PageOverlay::OverlayType::Document))
    , m_color(regionColor)
{
}

RegionOverlay::~RegionOverlay()
{
    RefPtr page = m_page.get();
    if (!page)
        return;
    if (RefPtr overlay = m_overlay)
        page->pageOverlayController().uninstallPageOverlay(*overlay, PageOverlay::FadeMode::DoNotFade);
}

void RegionOverlay::willMoveToPage(PageOverlay&, Page* page)
{
    if (!page)
        m_overlay = nullptr;
}

void RegionOverlay::didMoveToPage(PageOverlay&, Page* page)
{
    if (page)
        setRegionChanged();
}

void RegionOverlay::drawRect(PageOverlay&, GraphicsContext& context, const IntRect& dirtyRect)
{
    context.clearRect(dirtyRect);

    if (!m_region)
        return;

    drawRegion(context, *m_region, m_color, dirtyRect);
}

void RegionOverlay::drawRegion(GraphicsContext& context, const Region& region, const Color& color, const IntRect& dirtyRect)
{
    GraphicsContextStateSaver saver(context);
    context.setFillColor(color);
    for (auto rect : region.rects()) {
        if (rect.intersects(dirtyRect))
            context.fillRect(rect);
    }
}

bool RegionOverlay::mouseEvent(PageOverlay&, const PlatformMouseEvent&)
{
    return false;
}

void RegionOverlay::didScrollFrame(PageOverlay&, LocalFrame&)
{
}

void RegionOverlay::recomputeRegion()
{
    if (!m_regionChanged)
        return;

    if (updateRegion())
        protectedOverlay()->setNeedsDisplay();

    m_regionChanged = false;
}

#if COMPILER(CLANG)
#pragma mark - DebugPageOverlays
#endif

DebugPageOverlays& DebugPageOverlays::singleton()
{
    if (!sharedDebugOverlays)
        sharedDebugOverlays = new DebugPageOverlays;

    return *sharedDebugOverlays;
}

static inline size_t indexOf(DebugPageOverlays::RegionType regionType)
{
    return static_cast<size_t>(regionType);
}

RegionOverlay& DebugPageOverlays::ensureRegionOverlayForPage(Page& page, RegionType regionType)
{
    auto it = m_pageRegionOverlays.find(page);
    if (it != m_pageRegionOverlays.end()) {
        auto& visualizer = it->value[indexOf(regionType)];
        if (!visualizer)
            visualizer = RegionOverlay::create(page, regionType);
        return *visualizer;
    }

    Vector<RefPtr<RegionOverlay>> visualizers(NumberOfRegionTypes);
    auto visualizer = RegionOverlay::create(page, regionType);
    visualizers[indexOf(regionType)] = visualizer.copyRef();
    m_pageRegionOverlays.add(page, WTFMove(visualizers));
    return visualizer;
}

void DebugPageOverlays::showRegionOverlay(Page& page, RegionType regionType)
{
    auto& visualizer = ensureRegionOverlayForPage(page, regionType);
    page.pageOverlayController().installPageOverlay(visualizer.overlay(), PageOverlay::FadeMode::DoNotFade);
}

void DebugPageOverlays::hideRegionOverlay(Page& page, RegionType regionType)
{
    auto it = m_pageRegionOverlays.find(page);
    if (it == m_pageRegionOverlays.end())
        return;
    auto& visualizer = it->value[indexOf(regionType)];
    if (!visualizer)
        return;
    page.pageOverlayController().uninstallPageOverlay(visualizer->overlay(), PageOverlay::FadeMode::DoNotFade);
    visualizer = nullptr;
}

void DebugPageOverlays::regionChanged(LocalFrame& frame, RegionType regionType)
{
    RefPtr page = frame.page();
    if (!page)
        return;

    if (RefPtr visualizer = regionOverlayForPage(*page, regionType))
        visualizer->setRegionChanged();
}

bool DebugPageOverlays::hasOverlaysForPage(Page& page) const
{
    return m_pageRegionOverlays.contains(page);
}

void DebugPageOverlays::updateRegionIfNecessary(Page& page, RegionType regionType)
{
    if (RefPtr visualizer = regionOverlayForPage(page, regionType))
        visualizer->recomputeRegion();
}

RegionOverlay* DebugPageOverlays::regionOverlayForPage(Page& page, RegionType regionType) const
{
    auto it = m_pageRegionOverlays.find(page);
    if (it == m_pageRegionOverlays.end())
        return nullptr;
    return it->value.at(indexOf(regionType)).get();
}

void DebugPageOverlays::updateOverlayRegionVisibility(Page& page, OptionSet<DebugOverlayRegions> visibleRegions)
{
    if (visibleRegions.contains(DebugOverlayRegions::NonFastScrollableRegion))
        showRegionOverlay(page, RegionType::NonFastScrollableRegion);
    else
        hideRegionOverlay(page, RegionType::NonFastScrollableRegion);

    if (visibleRegions.contains(DebugOverlayRegions::WheelEventHandlerRegion))
        showRegionOverlay(page, RegionType::WheelEventHandlers);
    else
        hideRegionOverlay(page, RegionType::WheelEventHandlers);
    
    if (visibleRegions.contains(DebugOverlayRegions::InteractionRegion))
        showRegionOverlay(page, RegionType::InteractionRegion);
    else
        hideRegionOverlay(page, RegionType::InteractionRegion);

    if (visibleRegions.contains(DebugOverlayRegions::SiteIsolationRegion))
        showRegionOverlay(page, RegionType::SiteIsolationRegion);
    else
        hideRegionOverlay(page, RegionType::SiteIsolationRegion);
}

void DebugPageOverlays::settingsChanged(Page& page)
{
    auto activeOverlayRegions = OptionSet<DebugOverlayRegions>::fromRaw(page.settings().visibleDebugOverlayRegions());
    if (!activeOverlayRegions && !hasOverlays(page))
        return;

    DebugPageOverlays::singleton().updateOverlayRegionVisibility(page, activeOverlayRegions);
}

bool DebugPageOverlays::shouldPaintOverlayIntoLayer(Page& page, RegionType regionType) const
{
    if (RefPtr overlay = regionOverlayForPage(page, regionType))
        return overlay->shouldPaintOverlayIntoLayer();
    return false;
}

}
