/*
 * Copyright (C) 2012-2025 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WebPage.h"

#if PLATFORM(IOS_FAMILY)

#import "DocumentEditingContext.h"
#import "DragInitiationResult.h"
#import "DrawingArea.h"
#import "EditingRange.h"
#import "EditorState.h"
#import "InteractionInformationAtPosition.h"
#import "KeyEventInterpretationContext.h"
#import "Logging.h"
#import "MessageSenderInlines.h"
#import "NativeWebKeyboardEvent.h"
#import "PDFPluginBase.h"
#import "PluginView.h"
#import "PrintInfo.h"
#import "RemoteLayerTreeDrawingArea.h"
#import "RemoteScrollingCoordinator.h"
#import "RemoteWebTouchEvent.h"
#import "RevealItem.h"
#import "SandboxUtilities.h"
#import "ShareableBitmapUtilities.h"
#import "SharedBufferReference.h"
#import "SyntheticEditingCommandType.h"
#import "TapHandlingResult.h"
#import "TextCheckingControllerProxy.h"
#import "UIKitSPI.h"
#import "UnifiedPDFPlugin.h"
#import "UserData.h"
#import "ViewGestureGeometryCollector.h"
#import "VisibleContentRectUpdateInfo.h"
#import "WKAccessibilityWebPageObjectIOS.h"
#import "WebAutocorrectionContext.h"
#import "WebAutocorrectionData.h"
#import "WebChromeClient.h"
#import "WebEventConversion.h"
#import "WebFrame.h"
#import "WebImage.h"
#import "WebPageInternals.h"
#import "WebPageMessages.h"
#import "WebPageProxyMessages.h"
#import "WebPreviewLoaderClient.h"
#import "WebProcess.h"
#import "WebTouchEvent.h"
#import <CoreText/CTFont.h>
#import <WebCore/Autofill.h>
#import <WebCore/AutofillElements.h>
#import <WebCore/BoundaryPointInlines.h>
#import <WebCore/Chrome.h>
#import <WebCore/ContentChangeObserver.h>
#import <WebCore/DOMTimerHoldingTank.h>
#import <WebCore/DataDetection.h>
#import <WebCore/DataDetectionResultsStorage.h>
#import <WebCore/DiagnosticLoggingClient.h>
#import <WebCore/DiagnosticLoggingKeys.h>
#import <WebCore/Document.h>
#import <WebCore/DocumentInlines.h>
#import <WebCore/DocumentLoader.h>
#import <WebCore/DocumentMarkerController.h>
#import <WebCore/DragController.h>
#import <WebCore/EditingHTMLConverter.h>
#import <WebCore/EditingInlines.h>
#import <WebCore/Editor.h>
#import <WebCore/EditorClient.h>
#import <WebCore/Element.h>
#import <WebCore/ElementAncestorIteratorInlines.h>
#import <WebCore/ElementAnimationContext.h>
#import <WebCore/EventHandler.h>
#import <WebCore/File.h>
#import <WebCore/FloatQuad.h>
#import <WebCore/FocusController.h>
#import <WebCore/FontCache.h>
#import <WebCore/FontCacheCoreText.h>
#import <WebCore/GeometryUtilities.h>
#import <WebCore/GraphicsLayer.h>
#import <WebCore/HTMLAreaElement.h>
#import <WebCore/HTMLAttachmentElement.h>
#import <WebCore/HTMLBodyElement.h>
#import <WebCore/HTMLElement.h>
#import <WebCore/HTMLElementTypeHelpers.h>
#import <WebCore/HTMLFormElement.h>
#import <WebCore/HTMLHRElement.h>
#import <WebCore/HTMLIFrameElement.h>
#import <WebCore/HTMLImageElement.h>
#import <WebCore/HTMLInputElement.h>
#import <WebCore/HTMLLabelElement.h>
#import <WebCore/HTMLModelElement.h>
#import <WebCore/HTMLOptGroupElement.h>
#import <WebCore/HTMLOptionElement.h>
#import <WebCore/HTMLPlugInElement.h>
#import <WebCore/HTMLSelectElement.h>
#import <WebCore/HTMLSummaryElement.h>
#import <WebCore/HTMLTextAreaElement.h>
#import <WebCore/HTMLVideoElement.h>
#import <WebCore/HandleUserInputEventResult.h>
#import <WebCore/HistoryItem.h>
#import <WebCore/HitTestResult.h>
#import <WebCore/HitTestSource.h>
#import <WebCore/Image.h>
#import <WebCore/ImageOverlay.h>
#import <WebCore/InputMode.h>
#import <WebCore/KeyboardEvent.h>
#import <WebCore/LibWebRTCProvider.h>
#import <WebCore/LocalFrame.h>
#import <WebCore/LocalFrameLoaderClient.h>
#import <WebCore/LocalFrameView.h>
#import <WebCore/MediaSessionManagerIOS.h>
#import <WebCore/Node.h>
#import <WebCore/NodeInlines.h>
#import <WebCore/NodeList.h>
#import <WebCore/NodeRenderStyle.h>
#import <WebCore/NotImplemented.h>
#import <WebCore/Page.h>
#import <WebCore/PagePasteboardContext.h>
#import <WebCore/Pasteboard.h>
#import <WebCore/PlatformKeyboardEvent.h>
#import <WebCore/PlatformMediaSessionManager.h>
#import <WebCore/PlatformMouseEvent.h>
#import <WebCore/PluginDocument.h>
#import <WebCore/PluginViewBase.h>
#import <WebCore/PointerCaptureController.h>
#import <WebCore/PointerCharacteristics.h>
#import <WebCore/PrintContext.h>
#import <WebCore/Quirks.h>
#import <WebCore/Range.h>
#import <WebCore/RemoteFrame.h>
#import <WebCore/RemoteFrameGeometryTransformer.h>
#import <WebCore/RemoteFrameView.h>
#import <WebCore/RenderBlock.h>
#import <WebCore/RenderBoxInlines.h>
#import <WebCore/RenderImage.h>
#import <WebCore/RenderLayer.h>
#import <WebCore/RenderLayerBacking.h>
#import <WebCore/RenderLayerScrollableArea.h>
#import <WebCore/RenderThemeIOS.h>
#import <WebCore/RenderVideo.h>
#import <WebCore/RenderView.h>
#import <WebCore/RenderedDocumentMarker.h>
#import <WebCore/ScrollableArea.h>
#import <WebCore/Settings.h>
#import <WebCore/ShadowRoot.h>
#import <WebCore/SharedBuffer.h>
#import <WebCore/SharedMemory.h>
#import <WebCore/StyleProperties.h>
#import <WebCore/TextIndicator.h>
#import <WebCore/TextIterator.h>
#import <WebCore/UserAgent.h>
#import <WebCore/UserGestureIndicator.h>
#import <WebCore/ViewportArguments.h>
#import <WebCore/VisibleUnits.h>
#import <WebCore/WebEvent.h>
#import <WebCore/WritingSuggestionData.h>
#import <pal/system/ios/UserInterfaceIdiom.h>
#import <wtf/CoroutineUtilities.h>
#import <wtf/MathExtras.h>
#import <wtf/MemoryPressureHandler.h>
#import <wtf/RuntimeApplicationChecks.h>
#import <wtf/Scope.h>
#import <wtf/SetForScope.h>
#import <wtf/cocoa/Entitlements.h>
#import <wtf/cocoa/SpanCocoa.h>
#import <wtf/text/MakeString.h>
#import <wtf/text/StringToIntegerConversion.h>
#import <wtf/text/TextBreakIterator.h>
#import <wtf/text/TextStream.h>
#import <wtf/text/WTFString.h>

#if ENABLE(ATTACHMENT_ELEMENT)
#import <WebCore/PromisedAttachmentInfo.h>
#endif

#import <pal/cocoa/RevealSoftLink.h>

#define WEBPAGE_RELEASE_LOG(channel, fmt, ...) RELEASE_LOG(channel, "%p - WebPage::" fmt, this, ##__VA_ARGS__)
#define WEBPAGE_RELEASE_LOG_ERROR(channel, fmt, ...) RELEASE_LOG_ERROR(channel, "%p - WebPage::" fmt, this, ##__VA_ARGS__)

namespace WebKit {
using namespace WebCore;

// FIXME: Unclear if callers in this file are correctly choosing which of these two functions to use.

static String plainTextForContext(const SimpleRange& range)
{
    return WebCore::plainTextReplacingNoBreakSpace(range);
}

static String plainTextForContext(const std::optional<SimpleRange>& range)
{
    return range ? plainTextForContext(*range) : emptyString();
}

static String plainTextForDisplay(const SimpleRange& range)
{
    return WebCore::plainTextReplacingNoBreakSpace(range, { }, true);
}

static String plainTextForDisplay(const std::optional<SimpleRange>& range)
{
    return range ? plainTextForDisplay(*range) : emptyString();
}

static void adjustCandidateAutocorrectionInFrame(const String& correction, LocalFrame& frame)
{
#if HAVE(AUTOCORRECTION_ENHANCEMENTS)
    auto startPosition = frame.selection().selection().start();
    auto endPosition = frame.selection().selection().end();

    auto firstPositionInEditableContent = startOfEditableContent(startPosition);

    auto referenceRange = makeSimpleRange(firstPositionInEditableContent, endPosition);
    if (!referenceRange)
        return;

    auto correctedRange = findPlainText(*referenceRange, correction, { FindOption::Backwards });
    if (correctedRange.collapsed())
        return;

    addMarker(correctedRange, WebCore::DocumentMarkerType::CorrectionIndicator);
#else
    UNUSED_PARAM(frame);
#endif
}

// WebCore stores the page scale factor as float instead of double. When we get a scale from WebCore,
// we need to ignore differences that are within a small rounding error, with enough leeway
// to handle rounding differences that may result from round-tripping through UIScrollView.
bool scalesAreEssentiallyEqual(float a, float b)
{
    const auto scaleFactorEpsilon = 0.01f;
    return WTF::areEssentiallyEqual(a, b, scaleFactorEpsilon);
}

void WebPage::platformDetach()
{
    [m_mockAccessibilityElement setWebPage:nullptr];
}
    
void WebPage::platformInitializeAccessibility(ShouldInitializeNSAccessibility)
{
    m_mockAccessibilityElement = adoptNS([[WKAccessibilityWebPageObject alloc] init]);
    [m_mockAccessibilityElement setWebPage:this];

    RefPtr localMainFrame = m_page->localMainFrame();
    if (localMainFrame)
        accessibilityTransferRemoteToken(accessibilityRemoteTokenData());
}

void WebPage::platformReinitialize()
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    accessibilityTransferRemoteToken(accessibilityRemoteTokenData());
}

RetainPtr<NSData> WebPage::accessibilityRemoteTokenData() const
{
    return WebCore::Accessibility::newAccessibilityRemoteToken([[NSUUID UUID] UUIDString]);
}

void WebPage::relayAccessibilityNotification(String&& notificationName, RetainPtr<NSData>&& notificationData)
{
    send(Messages::WebPageProxy::RelayAccessibilityNotification(WTFMove(notificationName), span(notificationData.get())));
}

static void computeEditableRootHasContentAndPlainText(const VisibleSelection& selection, EditorState::PostLayoutData& data)
{
    data.hasContent = false;
    data.hasPlainText = false;
    if (!selection.isContentEditable())
        return;

    if (data.selectedTextLength || data.characterAfterSelection || data.characterBeforeSelection || data.twoCharacterBeforeSelection) {
        // If any of these variables have been previously set, the editable root must have plain text content, so we can bail from the remainder of the check.
        data.hasContent = true;
        data.hasPlainText = true;
        return;
    }

    auto* root = selection.rootEditableElement();
    if (!root || editingIgnoresContent(*root))
        return;

    auto startInEditableRoot = firstPositionInNode(root);
    data.hasContent = root->hasChildNodes() && !isEndOfEditableOrNonEditableContent(startInEditableRoot);
    if (data.hasContent) {
        auto range = makeSimpleRange(VisiblePosition { startInEditableRoot }, VisiblePosition { lastPositionInNode(root) });
        data.hasPlainText = range && hasAnyPlainText(*range);
    }
}

bool WebPage::requiresPostLayoutDataForEditorState(const LocalFrame& frame) const
{
    // If we have a composition or are using a hardware keyboard then we need to send the full
    // editor so that the UIProcess can update UI, including the position of the caret.
    bool needsLayout = frame.editor().hasComposition();
#if !PLATFORM(MACCATALYST)
    needsLayout |= m_keyboardIsAttached;
#endif
    return needsLayout;
}

static void convertContentToRootView(const LocalFrameView& view, Vector<SelectionGeometry>& geometries)
{
    for (auto& geometry : geometries)
        geometry.setQuad(view.contentsToRootView(geometry.quad()));
}

void WebPage::getPlatformEditorState(LocalFrame& frame, EditorState& result) const
{
    getPlatformEditorStateCommon(frame, result);

    if (!result.hasPostLayoutAndVisualData())
        return;

    ASSERT(frame.view());
    auto& postLayoutData = *result.postLayoutData;
    auto& visualData = *result.visualData;

    Ref view = *frame.view();

    if (frame.editor().hasComposition()) {
        if (auto compositionRange = frame.editor().compositionRange()) {
            visualData.markedTextRects = RenderObject::collectSelectionGeometries(*compositionRange).geometries;
            convertContentToRootView(view, visualData.markedTextRects);

            postLayoutData.markedText = plainTextForContext(*compositionRange);
            VisibleSelection compositionSelection(*compositionRange);
            visualData.markedTextCaretRectAtStart = view->contentsToRootView(compositionSelection.visibleStart().absoluteCaretBounds(nullptr /* insideFixed */));
            visualData.markedTextCaretRectAtEnd = view->contentsToRootView(compositionSelection.visibleEnd().absoluteCaretBounds(nullptr /* insideFixed */));
        }
    }

    const auto& selection = frame.selection().selection();
    std::optional<SimpleRange> selectedRange;
    postLayoutData.isStableStateUpdate = m_isInStableState;
    bool startNodeIsInsideFixedPosition = false;
    bool endNodeIsInsideFixedPosition = false;
    if (selection.isCaret()) {
        visualData.caretRectAtStart = view->contentsToRootView(frame.selection().absoluteCaretBounds(&startNodeIsInsideFixedPosition));
        endNodeIsInsideFixedPosition = startNodeIsInsideFixedPosition;
        visualData.caretRectAtEnd = visualData.caretRectAtStart;
        // FIXME: The following check should take into account writing direction.
        postLayoutData.isReplaceAllowed = result.isContentEditable && atBoundaryOfGranularity(selection.start(), TextGranularity::WordGranularity, SelectionDirection::Forward);

        selectedRange = wordRangeFromPosition(selection.start());
        postLayoutData.wordAtSelection = plainTextForContext(selectedRange);

        if (selection.isContentEditable())
            charactersAroundPosition(selection.start(), postLayoutData.characterAfterSelection, postLayoutData.characterBeforeSelection, postLayoutData.twoCharacterBeforeSelection);
    } else if (selection.isRange()) {
        visualData.caretRectAtStart = view->contentsToRootView(VisiblePosition(selection.start()).absoluteCaretBounds(&startNodeIsInsideFixedPosition));
        visualData.caretRectAtEnd = view->contentsToRootView(VisiblePosition(selection.end()).absoluteCaretBounds(&endNodeIsInsideFixedPosition));
        selectedRange = selection.toNormalizedRange();
        String selectedText;
        if (selectedRange) {
            auto [selectionGeometries, intersectingLayerIDs] = RenderObject::collectSelectionGeometries(*selectedRange);
            convertContentToRootView(view, selectionGeometries);
            selectedText = plainTextForDisplay(*selectedRange);
            postLayoutData.selectedTextLength = selectedText.length();
            const int maxSelectedTextLength = 200;
            postLayoutData.wordAtSelection = selectedText.left(maxSelectedTextLength);
            auto findSelectedEditableImageElement = [&] {
                RefPtr<HTMLImageElement> foundImage;
                if (!result.isContentEditable)
                    return foundImage;

                for (TextIterator iterator { *selectedRange, { } }; !iterator.atEnd(); iterator.advance()) {
                    auto imageElement = dynamicDowncast<HTMLImageElement>(iterator.node());
                    if (!imageElement)
                        continue;

                    if (foundImage) {
                        foundImage = nullptr;
                        break;
                    }

                    foundImage = imageElement;
                }
                return foundImage;
            };

            if (auto imageElement = findSelectedEditableImageElement())
                postLayoutData.selectedEditableImage = contextForElement(*imageElement);

            visualData.selectionGeometries = WTFMove(selectionGeometries);
            visualData.intersectingLayerIDs = WTFMove(intersectingLayerIDs);
        }
        // FIXME: We should disallow replace when the string contains only CJ characters.
        postLayoutData.isReplaceAllowed = result.isContentEditable && !result.isInPasswordField && !selectedText.containsOnly<isASCIIWhitespace>();
    }

#if USE(DICTATION_ALTERNATIVES)
    if (selectedRange) {
        auto markers = frame.document()->markers().markersInRange(*selectedRange, DocumentMarkerType::DictationAlternatives);
        postLayoutData.dictationContextsForSelection = WTF::map(markers, [] (auto& marker) {
            return std::get<DocumentMarker::DictationData>(marker->data()).context;
        });
    }
#endif

    postLayoutData.insideFixedPosition = startNodeIsInsideFixedPosition || endNodeIsInsideFixedPosition;
    if (!selection.isNone()) {
        if (selection.hasEditableStyle()) {
            // FIXME: The caret color style should be computed using the selection caret's container
            // rather than the focused element. This causes caret colors in editable children to be
            // ignored in favor of the editing host's caret color. See: <https://webkit.org/b/229809>.
            if (RefPtr editableRoot = selection.rootEditableElement(); editableRoot && editableRoot->renderer()) {
                auto& style = editableRoot->renderer()->style();
                postLayoutData.caretColor = CaretBase::computeCaretColor(style, editableRoot.get());
                postLayoutData.hasCaretColorAuto = style.hasAutoCaretColor();
                postLayoutData.hasGrammarDocumentMarkers = editableRoot->document().markers().hasMarkers(makeRangeSelectingNodeContents(*editableRoot), DocumentMarkerType::Grammar);
            }
        }

        computeEditableRootHasContentAndPlainText(selection, postLayoutData);
        postLayoutData.selectionStartIsAtParagraphBoundary = atBoundaryOfGranularity(selection.visibleStart(), TextGranularity::ParagraphGranularity, SelectionDirection::Backward);
        postLayoutData.selectionEndIsAtParagraphBoundary = atBoundaryOfGranularity(selection.visibleEnd(), TextGranularity::ParagraphGranularity, SelectionDirection::Forward);
    }
}

void WebPage::platformWillPerformEditingCommand()
{
#if ENABLE(CONTENT_CHANGE_OBSERVER)
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    if (RefPtr document = frame->document()) {
        if (auto* holdingTank = document->domTimerHoldingTankIfExists())
            holdingTank->removeAll();
    }
#endif
}

FloatSize WebPage::screenSize() const
{
    return m_screenSize;
}

FloatSize WebPage::availableScreenSize() const
{
    return m_availableScreenSize;
}

FloatSize WebPage::overrideScreenSize() const
{
    return m_overrideScreenSize;
}

FloatSize WebPage::overrideAvailableScreenSize() const
{
    return m_overrideAvailableScreenSize;
}

void WebPage::didReceiveMobileDocType(bool isMobileDoctype)
{
    m_isMobileDoctype = isMobileDoctype;
    resetViewportDefaultConfiguration(m_mainFrame.ptr(), isMobileDoctype);
}

void WebPage::savePageState(HistoryItem& historyItem)
{
    historyItem.setScaleIsInitial(!m_userHasChangedPageScaleFactor);
    historyItem.setMinimumLayoutSizeInScrollViewCoordinates(m_viewportConfiguration.minimumLayoutSize());
    historyItem.setContentSize(m_viewportConfiguration.contentsSize());
}

static double scaleAfterViewportWidthChange(double currentScale, bool scaleToFitContent, const ViewportConfiguration& viewportConfiguration, float unobscuredWidthInScrollViewCoordinates, const IntSize& newContentSize, const IntSize& oldContentSize, float visibleHorizontalFraction)
{
    double scale;
    if (!scaleToFitContent) {
        scale = viewportConfiguration.initialScale();
        LOG(VisibleRects, "scaleAfterViewportWidthChange using initial scale: %.2f", scale);
        return scale;
    }

    // When the content size changes, we keep the same relative horizontal content width in view, otherwise we would
    // end up zoomed too far in landscape->portrait, and too close in portrait->landscape.
    double widthToKeepInView = visibleHorizontalFraction * newContentSize.width();
    double newScale = unobscuredWidthInScrollViewCoordinates / widthToKeepInView;
    scale = std::max(std::min(newScale, viewportConfiguration.maximumScale()), viewportConfiguration.minimumScale());
    LOG(VisibleRects, "scaleAfterViewportWidthChange scaling content to fit: %.2f", scale);
    return scale;
}

static FloatPoint relativeCenterAfterContentSizeChange(const FloatRect& originalContentRect, IntSize oldContentSize, IntSize newContentSize)
{
    // If the content size has changed, keep the same relative position.
    FloatPoint oldContentCenter = originalContentRect.center();
    float relativeHorizontalPosition = oldContentCenter.x() / oldContentSize.width();
    float relativeVerticalPosition =  oldContentCenter.y() / oldContentSize.height();
    return FloatPoint(relativeHorizontalPosition * newContentSize.width(), relativeVerticalPosition * newContentSize.height());
}

static inline FloatRect adjustExposedRectForNewScale(const FloatRect& exposedRect, double exposedRectScale, double newScale)
{
    if (exposedRectScale == newScale)
        return exposedRect;

    float horizontalChange = exposedRect.width() * exposedRectScale / newScale - exposedRect.width();
    float verticalChange = exposedRect.height() * exposedRectScale / newScale - exposedRect.height();

    auto adjustedRect = exposedRect;
    adjustedRect.inflate({ horizontalChange / 2, verticalChange / 2 });
    return adjustedRect;
}

void WebPage::restorePageState(const HistoryItem& historyItem)
{
    // When a HistoryItem is cleared, its scale factor and scroll point are set to zero. We should not try to restore the other
    // parameters in those conditions.
    if (!historyItem.pageScaleFactor()) {
        send(Messages::WebPageProxy::CouldNotRestorePageState());
        return;
    }

    // We can restore the exposed rect and scale, but we cannot touch the scroll position since the obscured insets
    // may be changing in the UIProcess. The UIProcess can update the position from the information we send and will then
    // scroll to the correct position through a regular VisibleContentRectUpdate.

    m_userHasChangedPageScaleFactor = !historyItem.scaleIsInitial();
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;
    auto& frameView = *localMainFrame->view();

    FloatSize currentMinimumLayoutSizeInScrollViewCoordinates = m_viewportConfiguration.minimumLayoutSize();
    if (historyItem.minimumLayoutSizeInScrollViewCoordinates() == currentMinimumLayoutSizeInScrollViewCoordinates) {
        float boundedScale = historyItem.scaleIsInitial() ? m_viewportConfiguration.initialScale() : historyItem.pageScaleFactor();
        boundedScale = std::min<float>(m_viewportConfiguration.maximumScale(), std::max<float>(m_viewportConfiguration.minimumScale(), boundedScale));
        scalePage(boundedScale, IntPoint());

        std::optional<FloatPoint> scrollPosition;
        if (historyItem.shouldRestoreScrollPosition()) {
            m_drawingArea->setExposedContentRect(historyItem.exposedContentRect());
            m_hasRestoredExposedContentRectAfterDidCommitLoad = true;
            scrollPosition = FloatPoint(historyItem.scrollPosition());
        }

        RELEASE_LOG(Scrolling, "WebPage::restorePageState with matching minimumLayoutSize; historyItem.shouldRestoreScrollPosition %d, scrollPosition.y %d", historyItem.shouldRestoreScrollPosition(), historyItem.scrollPosition().y());
        send(Messages::WebPageProxy::RestorePageState(scrollPosition, frameView.scrollOrigin(), historyItem.obscuredInsets(), boundedScale));
    } else {
        IntSize oldContentSize = historyItem.contentSize();
        IntSize newContentSize = frameView.contentsSize();
        double visibleHorizontalFraction = static_cast<float>(historyItem.unobscuredContentRect().width()) / oldContentSize.width();

        double newScale = scaleAfterViewportWidthChange(historyItem.pageScaleFactor(), !historyItem.scaleIsInitial(), m_viewportConfiguration, currentMinimumLayoutSizeInScrollViewCoordinates.width(), newContentSize, oldContentSize, visibleHorizontalFraction);

        std::optional<FloatPoint> newCenter;
        if (historyItem.shouldRestoreScrollPosition()) {
            if (!oldContentSize.isEmpty() && !newContentSize.isEmpty() && newContentSize != oldContentSize)
                newCenter = relativeCenterAfterContentSizeChange(historyItem.unobscuredContentRect(), oldContentSize, newContentSize);
            else
                newCenter = FloatRect(historyItem.unobscuredContentRect()).center();
        }

        RELEASE_LOG(Scrolling, "WebPage::restorePageState with mismatched minimumLayoutSize; historyItem.shouldRestoreScrollPosition %d, unobscured rect top %d, scale %.2f", historyItem.shouldRestoreScrollPosition(), historyItem.unobscuredContentRect().y(), newScale);
        scalePage(newScale, IntPoint());
        send(Messages::WebPageProxy::RestorePageCenterAndScale(newCenter, newScale));
    }
}

double WebPage::minimumPageScaleFactor() const
{
    if (!m_viewportConfiguration.allowsUserScaling())
        return m_page->pageScaleFactor();
    return m_viewportConfiguration.minimumScale();
}

double WebPage::maximumPageScaleFactor() const
{
    if (!m_viewportConfiguration.allowsUserScaling())
        return m_page->pageScaleFactor();
    return m_viewportConfiguration.maximumScale();
}

double WebPage::maximumPageScaleFactorIgnoringAlwaysScalable() const
{
    if (!m_viewportConfiguration.allowsUserScalingIgnoringAlwaysScalable())
        return m_page->pageScaleFactor();
    return m_viewportConfiguration.maximumScaleIgnoringAlwaysScalable();
}

bool WebPage::allowsUserScaling() const
{
    return m_viewportConfiguration.allowsUserScaling();
}

bool WebPage::handleEditingKeyboardEvent(KeyboardEvent& event)
{
    auto* platformEvent = event.underlyingPlatformEvent();
    if (!platformEvent)
        return false;

    // Don't send synthetic events to the UIProcess. They are only
    // used for interacting with JavaScript.
    if (platformEvent->isSyntheticEvent())
        return false;

    if (handleKeyEventByRelinquishingFocusToChrome(event))
        return true;

    updateLastNodeBeforeWritingSuggestions(event);

    auto scrollingNodeID = [&] -> std::optional<WebCore::ScrollingNodeID> {
        RefPtr frame = m_page->focusController().focusedOrMainFrame();
        if (!frame)
            return std::nullopt;

        auto scrollableArea = frame->eventHandler().focusedScrollableArea();
        if (!scrollableArea)
            return std::nullopt;

        return scrollableArea->scrollingNodeID();
    }();

    auto isCharEvent = platformEvent->type() == PlatformKeyboardEvent::Type::Char;
    auto context = KeyEventInterpretationContext { isCharEvent, scrollingNodeID };

    // FIXME: Interpret the event immediately upon receiving it in UI process, without sending to WebProcess first.
    auto sendResult = WebProcess::singleton().parentProcessConnection()->sendSync(Messages::WebPageProxy::InterpretKeyEvent(editorState(ShouldPerformLayout::Yes), context), m_identifier);
    auto [eventWasHandled] = sendResult.takeReplyOr(false);
    return eventWasHandled;
}

static bool disableServiceWorkerEntitlementTestingOverride;

bool WebPage::parentProcessHasServiceWorkerEntitlement() const
{
    if (disableServiceWorkerEntitlementTestingOverride)
        return false;
    
    static bool hasEntitlement = WTF::hasEntitlement(WebProcess::singleton().parentProcessConnection()->xpcConnection(), "com.apple.developer.WebKit.ServiceWorkers"_s) || WTF::hasEntitlement(WebProcess::singleton().parentProcessConnection()->xpcConnection(), "com.apple.developer.web-browser"_s);
    return hasEntitlement;
}

void WebPage::disableServiceWorkerEntitlement()
{
    disableServiceWorkerEntitlementTestingOverride = true;
}

void WebPage::clearServiceWorkerEntitlementOverride(CompletionHandler<void()>&& completionHandler)
{
    disableServiceWorkerEntitlementTestingOverride = false;
    completionHandler();
}

bool WebPage::performNonEditingBehaviorForSelector(const String&, WebCore::KeyboardEvent*)
{
    notImplemented();
    return false;
}

void WebPage::getSelectionContext(CompletionHandler<void(const String&, const String&, const String&)>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    static constexpr auto selectionExtendedContextLength = 350;

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = focusedPluginViewForFrame(*frame)) {
        auto [textBefore, textAfter] = pluginView->stringsBeforeAndAfterSelection(selectionExtendedContextLength);
        return completionHandler(pluginView->selectionString(), WTFMove(textBefore), WTFMove(textAfter));
    }
#endif

    if (!frame->selection().isRange())
        return completionHandler({ }, { }, { });

    auto& selection = frame->selection().selection();
    String selectedText = plainTextForContext(selection.firstRange());
    String textBefore = plainTextForDisplay(rangeExpandedByCharactersInDirectionAtWordBoundary(selection.start(), selectionExtendedContextLength, SelectionDirection::Backward));
    String textAfter = plainTextForDisplay(rangeExpandedByCharactersInDirectionAtWordBoundary(selection.end(), selectionExtendedContextLength, SelectionDirection::Forward));

    completionHandler(selectedText, textBefore, textAfter);
}

NSObject *WebPage::accessibilityObjectForMainFramePlugin()
{
#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = mainFramePlugIn())
        return pluginView->accessibilityObject();
#endif

    return nil;
}
    
void WebPage::updateRemotePageAccessibilityOffset(WebCore::FrameIdentifier, WebCore::IntPoint offset)
{
    [accessibilityRemoteObject() setRemoteFrameOffset:offset];
}

void WebPage::registerRemoteFrameAccessibilityTokens(pid_t pid, std::span<const uint8_t> elementToken, WebCore::FrameIdentifier frameID)
{
    createMockAccessibilityElement(pid);
    [m_mockAccessibilityElement setRemoteTokenData:toNSData(elementToken).get()];
    [m_mockAccessibilityElement setFrameIdentifier:frameID];
}

void WebPage::createMockAccessibilityElement(pid_t pid)
{
    auto mockAccessibilityElement = adoptNS([[WKAccessibilityWebPageObject alloc] init]);

    [mockAccessibilityElement setWebPage:this];
    m_mockAccessibilityElement = WTFMove(mockAccessibilityElement);
}

void WebPage::registerUIProcessAccessibilityTokens(std::span<const uint8_t> elementToken, std::span<const uint8_t>)
{
    [m_mockAccessibilityElement setRemoteTokenData:toNSData(elementToken).get()];
}

void WebPage::getStringSelectionForPasteboard(CompletionHandler<void(String&&)>&& completionHandler)
{
    notImplemented();
    completionHandler({ });
}

void WebPage::getDataSelectionForPasteboard(const String, CompletionHandler<void(RefPtr<SharedBuffer>&&)>&& completionHandler)
{
    notImplemented();
    completionHandler({ });
}

WebCore::IntPoint WebPage::accessibilityRemoteFrameOffset()
{
    return [m_mockAccessibilityElement accessibilityRemoteFrameOffset];
}

WKAccessibilityWebPageObject* WebPage::accessibilityRemoteObject()
{
    return m_mockAccessibilityElement.get();
}

bool WebPage::platformCanHandleRequest(const WebCore::ResourceRequest& request)
{
    NSURLRequest *nsRequest = request.nsURLRequest(HTTPBodyUpdatePolicy::DoNotUpdateHTTPBody);
    if (!nsRequest.URL)
        return false;

    return [NSURLConnection canHandleRequest:nsRequest];
}

void WebPage::shouldDelayWindowOrderingEvent(const WebKit::WebMouseEvent&, CompletionHandler<void(bool)>&& completionHandler)
{
    notImplemented();
    completionHandler(false);
}

void WebPage::advanceToNextMisspelling(bool)
{
    notImplemented();
}

IntRect WebPage::rectForElementAtInteractionLocation() const
{
    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return IntRect();
    HitTestResult result = localMainFrame->eventHandler().hitTestResultAtPoint(m_lastInteractionLocation, hitType);
    Node* hitNode = result.innerNode();
    if (!hitNode || !hitNode->renderer())
        return IntRect();
    return result.innerNodeFrame()->view()->contentsToRootView(hitNode->renderer()->absoluteBoundingBoxRect(true));
}

void WebPage::updateSelectionAppearance()
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    auto& editor = frame->editor();
    if (editor.ignoreSelectionChanges())
        return;

    if (editor.client() && !editor.client()->shouldRevealCurrentSelectionAfterInsertion())
        return;

    if (!editor.hasComposition() && frame->selection().selection().isNone())
        return;

    didChangeSelection(*frame);
}

static void dispatchSyntheticMouseMove(LocalFrame& mainFrame, const WebCore::FloatPoint& location, OptionSet<WebEventModifier> modifiers, WebCore::PointerID pointerId = WebCore::mousePointerID)
{
    IntPoint roundedAdjustedPoint = roundedIntPoint(location);
    auto mouseEvent = PlatformMouseEvent(roundedAdjustedPoint, roundedAdjustedPoint, MouseButton::None, PlatformEvent::Type::MouseMoved, 0, platform(modifiers), WallTime::now(), WebCore::ForceAtClick, WebCore::SyntheticClickType::OneFingerTap, pointerId);
    // FIXME: Pass caps lock state.
    mainFrame.eventHandler().dispatchSyntheticMouseMove(mouseEvent);
}

void WebPage::generateSyntheticEditingCommand(SyntheticEditingCommandType command)
{
    PlatformKeyboardEvent keyEvent;
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;
    
    OptionSet<PlatformEvent::Modifier> modifiers;
    modifiers.add(PlatformEvent::Modifier::MetaKey);
    
    switch (command) {
    case SyntheticEditingCommandType::Undo:
        keyEvent = PlatformKeyboardEvent(PlatformEvent::Type::KeyDown, "z"_s, "z"_s,
        "z"_s, "KeyZ"_s,
        "U+005A"_s, 90, false, false, false, modifiers, WallTime::now());
        break;
    case SyntheticEditingCommandType::Redo:
        keyEvent = PlatformKeyboardEvent(PlatformEvent::Type::KeyDown, "y"_s, "y"_s,
        "y"_s, "KeyY"_s,
        "U+0059"_s, 89, false, false, false, modifiers, WallTime::now());
        break;
    case SyntheticEditingCommandType::ToggleBoldface:
        keyEvent = PlatformKeyboardEvent(PlatformEvent::Type::KeyDown, "b"_s, "b"_s,
        "b"_s, "KeyB"_s,
        "U+0042"_s, 66, false, false, false, modifiers, WallTime::now());
        break;
    case SyntheticEditingCommandType::ToggleItalic:
        keyEvent = PlatformKeyboardEvent(PlatformEvent::Type::KeyDown, "i"_s, "i"_s,
        "i"_s, "KeyI"_s,
        "U+0049"_s, 73, false, false, false, modifiers, WallTime::now());
        break;
    case SyntheticEditingCommandType::ToggleUnderline:
        keyEvent = PlatformKeyboardEvent(PlatformEvent::Type::KeyDown, "u"_s, "u"_s,
        "u"_s, "KeyU"_s,
        "U+0055"_s, 85, false, false, false, modifiers, WallTime::now());
        break;
    default:
        break;
    }

    keyEvent.setIsSyntheticEvent();
    
    PlatformKeyboardEvent::setCurrentModifierState(modifiers);
    
    frame->eventHandler().keyEvent(keyEvent);
}

void WebPage::handleSyntheticClick(std::optional<WebCore::FrameIdentifier> frameID, Node& nodeRespondingToClick, const WebCore::FloatPoint& location, OptionSet<WebEventModifier> modifiers, WebCore::PointerID pointerId)
{
    auto& respondingDocument = nodeRespondingToClick.document();
    m_hasHandledSyntheticClick = true;

    if (!respondingDocument.settings().contentChangeObserverEnabled() || respondingDocument.quirks().shouldIgnoreContentObservationForClick(nodeRespondingToClick)) {
        completeSyntheticClick(frameID, nodeRespondingToClick, location, modifiers, WebCore::SyntheticClickType::OneFingerTap, pointerId);
        return;
    }

    auto& contentChangeObserver = respondingDocument.contentChangeObserver();
    contentChangeObserver.setClickTarget(nodeRespondingToClick);
    auto targetNodeWentFromHiddenToVisible = contentChangeObserver.hiddenTouchTarget() == &nodeRespondingToClick && ContentChangeObserver::isConsideredVisible(nodeRespondingToClick);
    {
        LOG_WITH_STREAM(ContentObservation, stream << "handleSyntheticClick: node(" << &nodeRespondingToClick << ") " << location);
        ContentChangeObserver::MouseMovedScope observingScope(respondingDocument);
        RefPtr localRootFrame = this->localRootFrame(frameID);
        if (!localRootFrame)
            return;
        dispatchSyntheticMouseMove(*localRootFrame, location, modifiers, pointerId);
        localRootFrame->protectedDocument()->updateStyleIfNeeded();
        if (m_isClosed)
            return;
    }

    if (targetNodeWentFromHiddenToVisible) {
        LOG(ContentObservation, "handleSyntheticClick: target node was hidden and now is visible -> hover.");
        didHandleTapAsHover();
        return;
    }

    auto nodeTriggersFastPath = [&](auto& targetNode) {
        RefPtr element = dynamicDowncast<Element>(targetNode);
        if (!element)
            return false;
        if (is<HTMLFormControlElement>(*element))
            return true;
        if (element->document().quirks().shouldIgnoreAriaForFastPathContentObservationCheck())
            return false;
        auto ariaRole = AccessibilityObject::ariaRoleToWebCoreRole(element->getAttribute(HTMLNames::roleAttr));
        return AccessibilityObject::isARIAControl(ariaRole);
    };
    auto targetNodeTriggersFastPath = nodeTriggersFastPath(nodeRespondingToClick);

    auto observedContentChange = contentChangeObserver.observedContentChange();
    auto continueContentObservation = !(observedContentChange == WKContentVisibilityChange || targetNodeTriggersFastPath);
    if (continueContentObservation) {
        // Wait for callback to didFinishContentChangeObserving() to decide whether to send the click event.
        const Seconds observationDuration = 32_ms;
        contentChangeObserver.startContentObservationForDuration(observationDuration);
        LOG(ContentObservation, "handleSyntheticClick: Can't decide it yet -> wait.");
        m_pendingSyntheticClickNode = nodeRespondingToClick;
        m_pendingSyntheticClickLocation = location;
        m_pendingSyntheticClickModifiers = modifiers;
        m_pendingSyntheticClickPointerId = pointerId;
        return;
    }
    contentChangeObserver.stopContentObservation();
    callOnMainRunLoop([protectedThis = Ref { *this }, targetNode = Ref<Node>(nodeRespondingToClick), location, modifiers, observedContentChange, pointerId, frameID] {
        if (protectedThis->m_isClosed || !protectedThis->corePage())
            return;

        auto shouldStayAtHoverState = observedContentChange == WKContentVisibilityChange;
        if (shouldStayAtHoverState) {
            // The move event caused new contents to appear. Don't send synthetic click event, but just ensure that the mouse is on the most recent content.
            if (RefPtr localRootFrame = protectedThis->localRootFrame(frameID))
                dispatchSyntheticMouseMove(*localRootFrame, location, modifiers, pointerId);
            LOG(ContentObservation, "handleSyntheticClick: Observed meaningful visible change -> hover.");
            protectedThis->didHandleTapAsHover();
            return;
        }
        LOG(ContentObservation, "handleSyntheticClick: calling completeSyntheticClick -> click.");
        protectedThis->completeSyntheticClick(frameID, targetNode, location, modifiers, WebCore::SyntheticClickType::OneFingerTap, pointerId);
    });
}

void WebPage::didHandleTapAsHover()
{
    invokePendingSyntheticClickCallback(SyntheticClickResult::Hover);
    send(Messages::WebPageProxy::DidHandleTapAsHover());
}

void WebPage::didFinishContentChangeObserving(WebCore::FrameIdentifier frameID, WKContentChange observedContentChange)
{
    LOG_WITH_STREAM(ContentObservation, stream << "didFinishContentChangeObserving: pending target node(" << m_pendingSyntheticClickNode << ")");
    if (!m_pendingSyntheticClickNode)
        return;
    callOnMainRunLoop([
        protectedThis = Ref { *this },
        targetNode = Ref<Node>(*m_pendingSyntheticClickNode),
        originalDocument = WeakPtr<Document, WeakPtrImplWithEventTargetData> { m_pendingSyntheticClickNode->document() },
        observedContentChange,
        location = m_pendingSyntheticClickLocation,
        modifiers = m_pendingSyntheticClickModifiers,
        pointerId = m_pendingSyntheticClickPointerId,
        frameID
    ] {
        if (protectedThis->m_isClosed || !protectedThis->corePage())
            return;
        if (!originalDocument || &targetNode->document() != originalDocument)
            return;

        // Only dispatch the click if the document didn't get changed by any timers started by the move event.
        if (observedContentChange == WKContentNoChange) {
            LOG(ContentObservation, "No change was observed -> click.");
            protectedThis->completeSyntheticClick(frameID, targetNode, location, modifiers, WebCore::SyntheticClickType::OneFingerTap, pointerId);
            return;
        }
        // Ensure that the mouse is on the most recent content.
        LOG(ContentObservation, "Observed meaningful visible change -> hover.");
        if (RefPtr localRootFrame = protectedThis->localRootFrame(frameID))
            dispatchSyntheticMouseMove(*localRootFrame, location, modifiers, pointerId);

        protectedThis->didHandleTapAsHover();
    });
    m_pendingSyntheticClickNode = nullptr;
    m_pendingSyntheticClickLocation = { };
    m_pendingSyntheticClickModifiers = { };
    m_pendingSyntheticClickPointerId = 0;
}

void WebPage::completeSyntheticClick(std::optional<WebCore::FrameIdentifier> frameID, Node& nodeRespondingToClick, const WebCore::FloatPoint& location, OptionSet<WebEventModifier> modifiers, SyntheticClickType syntheticClickType, WebCore::PointerID pointerId)
{
    SetForScope completeSyntheticClickScope { m_completingSyntheticClick, true };
    IntPoint roundedAdjustedPoint = roundedIntPoint(location);
    RefPtr localRootFrame = this->localRootFrame(frameID);
    if (!localRootFrame) {
        invokePendingSyntheticClickCallback(SyntheticClickResult::PageInvalid);
        return;
    }

    RefPtr oldFocusedFrame = m_page->focusController().focusedLocalFrame();
    RefPtr<Element> oldFocusedElement = oldFocusedFrame ? oldFocusedFrame->document()->focusedElement() : nullptr;

    SetForScope userIsInteractingChange { m_userIsInteracting, true };

    m_lastInteractionLocation = roundedAdjustedPoint;

    // FIXME: Pass caps lock state.
    auto platformModifiers = platform(modifiers);

    bool handledPress = localRootFrame->eventHandler().handleMousePressEvent(PlatformMouseEvent(roundedAdjustedPoint, roundedAdjustedPoint, MouseButton::Left, PlatformEvent::Type::MousePressed, 1, platformModifiers, WallTime::now(), WebCore::ForceAtClick, syntheticClickType, pointerId)).wasHandled();
    if (m_isClosed)
        return;

    if (auto selectionChangedHandler = std::exchange(m_selectionChangedHandler, {}))
        selectionChangedHandler();
    else if (!handledPress)
        clearSelectionAfterTapIfNeeded();

    auto releaseEvent = PlatformMouseEvent { roundedAdjustedPoint, roundedAdjustedPoint, MouseButton::Left, PlatformEvent::Type::MouseReleased, 1, platformModifiers, WallTime::now(), ForceAtClick, syntheticClickType, pointerId };
    bool handledRelease = localRootFrame->eventHandler().handleMouseReleaseEvent(releaseEvent).wasHandled();
    if (m_isClosed)
        return;

    RefPtr newFocusedFrame = m_page->focusController().focusedLocalFrame();
    RefPtr<Element> newFocusedElement = newFocusedFrame ? newFocusedFrame->document()->focusedElement() : nullptr;

    if (nodeRespondingToClick.document().settings().contentChangeObserverEnabled()) {
        auto& document = nodeRespondingToClick.document();
        // Dispatch mouseOut to dismiss tooltip content when tapping on the control bar buttons (cc, settings).
        if (document.quirks().needsYouTubeMouseOutQuirk()) {
            if (RefPtr frame = document.frame()) {
                PlatformMouseEvent event { roundedAdjustedPoint, roundedAdjustedPoint, MouseButton::Left, PlatformEvent::Type::NoType, 0, platformModifiers, WallTime::now(), 0, WebCore::SyntheticClickType::NoTap, pointerId };
                if (!nodeRespondingToClick.isConnected())
                    frame->eventHandler().dispatchSyntheticMouseMove(event);
                frame->eventHandler().dispatchSyntheticMouseOut(event);
            }
        }
    }

    if (m_isClosed)
        return;

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginElement = dynamicDowncast<HTMLPlugInElement>(nodeRespondingToClick)) {
        if (RefPtr pluginWidget = static_cast<PluginView*>(pluginElement->pluginWidget()))
            pluginWidget->handleSyntheticClick(WTFMove(releaseEvent));
    }
#endif

    invokePendingSyntheticClickCallback(SyntheticClickResult::Click);

    if ((!handledPress && !handledRelease) || !nodeRespondingToClick.isElementNode())
        send(Messages::WebPageProxy::DidNotHandleTapAsClick(roundedIntPoint(location)));

    send(Messages::WebPageProxy::DidCompleteSyntheticClick());

    scheduleLayoutViewportHeightExpansionUpdate();
}

void WebPage::attemptSyntheticClick(const IntPoint& point, OptionSet<WebEventModifier> modifiers, TransactionID lastLayerTreeTransactionId)
{
    FloatPoint adjustedPoint;
    RefPtr localMainFrame = m_page->localMainFrame();
    Node* nodeRespondingToClick = localMainFrame ? localMainFrame->nodeRespondingToClickEvents(point, adjustedPoint) : nullptr;
    auto* frameRespondingToClick = nodeRespondingToClick ? nodeRespondingToClick->document().frame() : nullptr;
    IntPoint adjustedIntPoint = roundedIntPoint(adjustedPoint);

    bool didNotHandleTapAsClick = !frameRespondingToClick;
    if (frameRespondingToClick) {
        auto firstTransactionID = WebFrame::fromCoreFrame(*frameRespondingToClick)->firstLayerTreeTransactionIDAfterDidCommitLoad();
        didNotHandleTapAsClick = !firstTransactionID || lastLayerTreeTransactionId.lessThanSameProcess(*firstTransactionID);
    }

    if (didNotHandleTapAsClick)
        send(Messages::WebPageProxy::DidNotHandleTapAsClick(adjustedIntPoint));
    else if (m_interactionNode == nodeRespondingToClick)
        completeSyntheticClick(std::nullopt, *nodeRespondingToClick, adjustedPoint, modifiers, WebCore::SyntheticClickType::OneFingerTap);
    else
        handleSyntheticClick(std::nullopt, *nodeRespondingToClick, adjustedPoint, modifiers);
}

void WebPage::handleDoubleTapForDoubleClickAtPoint(const IntPoint& point, OptionSet<WebEventModifier> modifiers, TransactionID lastLayerTreeTransactionId)
{
    FloatPoint adjustedPoint;
    RefPtr localMainFrame = m_page->localMainFrame();
    auto* nodeRespondingToDoubleClick = localMainFrame ? localMainFrame->nodeRespondingToDoubleClickEvent(point, adjustedPoint) : nullptr;
    if (!nodeRespondingToDoubleClick)
        return;

    auto* frameRespondingToDoubleClick = nodeRespondingToDoubleClick->document().frame();
    if (!frameRespondingToDoubleClick)
        return;

    auto firstTransactionID = WebFrame::fromCoreFrame(*frameRespondingToDoubleClick)->firstLayerTreeTransactionIDAfterDidCommitLoad();
    if (!firstTransactionID || lastLayerTreeTransactionId.lessThanSameProcess(*firstTransactionID))
        return;

    SetForScope userIsInteractingChange { m_userIsInteracting, true };

    auto platformModifiers = platform(modifiers);
    auto roundedAdjustedPoint = roundedIntPoint(adjustedPoint);
    nodeRespondingToDoubleClick->document().frame()->eventHandler().handleMousePressEvent(PlatformMouseEvent(roundedAdjustedPoint, roundedAdjustedPoint, MouseButton::Left, PlatformEvent::Type::MousePressed, 2, platformModifiers, WallTime::now(), 0, WebCore::SyntheticClickType::OneFingerTap));
    if (m_isClosed)
        return;
    nodeRespondingToDoubleClick->document().frame()->eventHandler().handleMouseReleaseEvent(PlatformMouseEvent(roundedAdjustedPoint, roundedAdjustedPoint, MouseButton::Left, PlatformEvent::Type::MouseReleased, 2, platformModifiers, WallTime::now(), 0, WebCore::SyntheticClickType::OneFingerTap));
}

void WebPage::requestFocusedElementInformation(CompletionHandler<void(const std::optional<FocusedElementInformation>&)>&& completionHandler)
{
    std::optional<FocusedElementInformation> information;
    if (m_focusedElement)
        information = focusedElementInformation();

    completionHandler(information);
}

void WebPage::updateFocusedElementInformation()
{
    m_updateFocusedElementInformationTimer.stop();

    if (!m_focusedElement)
        return;

    auto information = focusedElementInformation();
    if (!information)
        return;

    send(Messages::WebPageProxy::UpdateFocusedElementInformation(*information));
}

#if ENABLE(DRAG_SUPPORT)
Awaitable<DragInitiationResult> WebPage::requestDragStart(std::optional<WebCore::FrameIdentifier> remoteFrameID, IntPoint clientPosition, IntPoint globalPosition, OptionSet<WebCore::DragSourceAction> allowedActionsMask)
{
    SetForScope allowedActionsForScope(m_allowedDragSourceActions, allowedActionsMask);
    RefPtr localRootFrame = this->localRootFrame(remoteFrameID);
    if (!localRootFrame)
        co_return { false };

    auto handledOrTransformer = co_await AwaitableFromCompletionHandler<Expected<bool, RemoteFrameGeometryTransformer>> { [=] (auto completionHandler) {
        localRootFrame->eventHandler().tryToBeginDragAtPoint(clientPosition, globalPosition, WTFMove(completionHandler));
    } };
    if (handledOrTransformer)
        co_return { *handledOrTransformer };
    auto& transformer = handledOrTransformer.error();
    co_return { DragInitiationResult::RemoteFrameData {
        transformer.remoteFrameID(),
        transformer.transformToRemoteFrameCoordinates(clientPosition),
        transformer.transformToRemoteFrameCoordinates(globalPosition)
    } };
}

Awaitable<DragInitiationResult> WebPage::requestAdditionalItemsForDragSession(std::optional<WebCore::FrameIdentifier> rootFrameID, IntPoint clientPosition, IntPoint globalPosition, OptionSet<WebCore::DragSourceAction> allowedActionsMask)
{
    SetForScope allowedActionsForScope(m_allowedDragSourceActions, allowedActionsMask);
    // To augment the platform drag session with additional items, end the current drag session and begin a new drag session with the new drag item.
    // This process is opaque to the UI process, which still maintains the old drag item in its drag session. Similarly, this persistent drag session
    // is opaque to the web process, which only sees that the current drag has ended, and that a new one is beginning.
    PlatformMouseEvent event(clientPosition, globalPosition, MouseButton::Left, PlatformEvent::Type::MouseMoved, 0, { }, WallTime::now(), 0, WebCore::SyntheticClickType::NoTap);
    m_page->dragController().dragEnded();
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        co_return { false };

    localMainFrame->eventHandler().dragSourceEndedAt(event, { }, MayExtendDragSession::Yes);

    auto handledOrTransformer = co_await AwaitableFromCompletionHandler<Expected<bool, RemoteFrameGeometryTransformer>> { [=] (auto completionHandler) {
        localMainFrame->eventHandler().tryToBeginDragAtPoint(clientPosition, globalPosition, WTFMove(completionHandler));
    } };
    if (handledOrTransformer)
        co_return { *handledOrTransformer };
    auto& transformer = handledOrTransformer.error();
    co_return { DragInitiationResult::RemoteFrameData {
        transformer.remoteFrameID(),
        transformer.transformToRemoteFrameCoordinates(clientPosition),
        transformer.transformToRemoteFrameCoordinates(globalPosition)
    } };
}

void WebPage::insertDroppedImagePlaceholders(const Vector<IntSize>& imageSizes, CompletionHandler<void(const Vector<IntRect>&, std::optional<WebCore::TextIndicatorData>)>&& reply)
{
    m_page->dragController().insertDroppedImagePlaceholdersAtCaret(imageSizes);
    auto placeholderRects = m_page->dragController().droppedImagePlaceholders().map([&] (auto& element) {
        return rootViewBounds(element);
    });

    auto imagePlaceholderRange = m_page->dragController().droppedImagePlaceholderRange();
    if (placeholderRects.size() != imageSizes.size()) {
        RELEASE_LOG(DragAndDrop, "Failed to insert dropped image placeholders: placeholder rect count (%tu) does not match image size count (%tu).", placeholderRects.size(), imageSizes.size());
        reply({ }, std::nullopt);
        return;
    }

    if (!imagePlaceholderRange) {
        RELEASE_LOG(DragAndDrop, "Failed to insert dropped image placeholders: no image placeholder range.");
        reply({ }, std::nullopt);
        return;
    }

    std::optional<TextIndicatorData> textIndicatorData;
    constexpr OptionSet<TextIndicatorOption> textIndicatorOptions {
        TextIndicatorOption::IncludeSnapshotOfAllVisibleContentWithoutSelection,
        TextIndicatorOption::ExpandClipBeyondVisibleRect,
        TextIndicatorOption::PaintAllContent,
        TextIndicatorOption::UseSelectionRectForSizing
    };

    if (auto textIndicator = TextIndicator::createWithRange(*imagePlaceholderRange, textIndicatorOptions, TextIndicatorPresentationTransition::None, { }))
        textIndicatorData = textIndicator->data();

    reply(WTFMove(placeholderRects), WTFMove(textIndicatorData));
}

void WebPage::didConcludeDrop()
{
    m_rangeForDropSnapshot = std::nullopt;
    m_pendingImageElementsForDropSnapshot.clear();
}

void WebPage::didConcludeEditDrag()
{
    send(Messages::WebPageProxy::WillReceiveEditDragSnapshot());

    layoutIfNeeded();

    m_pendingImageElementsForDropSnapshot.clear();

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    if (auto selectionRange = frame->selection().selection().toNormalizedRange()) {
        m_pendingImageElementsForDropSnapshot = visibleImageElementsInRangeWithNonLoadedImages(*selectionRange);
        frame->selection().setSelectedRange(makeSimpleRange(selectionRange->end), Affinity::Downstream, FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
        m_rangeForDropSnapshot = WTFMove(selectionRange);
    }

    if (m_pendingImageElementsForDropSnapshot.isEmpty())
        computeAndSendEditDragSnapshot();
}

void WebPage::didFinishLoadingImageForElement(WebCore::HTMLImageElement& element)
{
    if (!m_pendingImageElementsForDropSnapshot.remove(&element))
        return;

    bool shouldSendSnapshot = m_pendingImageElementsForDropSnapshot.isEmpty();
    m_page->dragController().finalizeDroppedImagePlaceholder(element, [protectedThis = Ref { *this }, shouldSendSnapshot] {
        if (shouldSendSnapshot)
            protectedThis->computeAndSendEditDragSnapshot();
    });
}

void WebPage::computeAndSendEditDragSnapshot()
{
    std::optional<TextIndicatorData> textIndicatorData;
    constexpr OptionSet<TextIndicatorOption> defaultTextIndicatorOptionsForEditDrag {
        TextIndicatorOption::IncludeSnapshotOfAllVisibleContentWithoutSelection,
        TextIndicatorOption::ExpandClipBeyondVisibleRect,
        TextIndicatorOption::PaintAllContent,
        TextIndicatorOption::IncludeMarginIfRangeMatchesSelection,
        TextIndicatorOption::PaintBackgrounds,
        TextIndicatorOption::ComputeEstimatedBackgroundColor,
        TextIndicatorOption::UseSelectionRectForSizing,
        TextIndicatorOption::IncludeSnapshotWithSelectionHighlight
    };
    if (auto range = std::exchange(m_rangeForDropSnapshot, std::nullopt)) {
        if (auto textIndicator = TextIndicator::createWithRange(*range, defaultTextIndicatorOptionsForEditDrag, TextIndicatorPresentationTransition::None, { }))
            textIndicatorData = textIndicator->data();
    }
    send(Messages::WebPageProxy::DidReceiveEditDragSnapshot(WTFMove(textIndicatorData)));
}

#endif

void WebPage::sendTapHighlightForNodeIfNecessary(WebKit::TapIdentifier requestID, Node* node, FloatPoint point)
{
#if ENABLE(TOUCH_EVENTS)
    if (!node)
        return;

    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;

    if (m_page->isEditable() && node == localMainFrame->document()->body())
        return;

    if (RefPtr element = dynamicDowncast<Element>(*node)) {
        ASSERT(m_page);
        localMainFrame->loader().prefetchDNSIfNeeded(element->absoluteLinkURL());
    }

    if (RefPtr area = dynamicDowncast<HTMLAreaElement>(*node)) {
        node = area->imageElement().get();
        if (!node)
            return;
    }

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = pluginViewForFrame(node->document().frame())) {
        if (auto rect = pluginView->highlightRectForTapAtPoint(point)) {
            auto highlightColor = RenderThemeIOS::singleton().platformTapHighlightColor();
            auto highlightQuads = Vector { FloatQuad { WTFMove(*rect) } };
            send(Messages::WebPageProxy::DidGetTapHighlightGeometries(requestID, WTFMove(highlightColor), WTFMove(highlightQuads), { }, { }, { }, { }, true));
            return;
        }
    }
#endif // ENABLE(PDF_PLUGIN)

    Vector<FloatQuad> quads;
    if (RenderObject *renderer = node->renderer()) {
        renderer->absoluteQuads(quads);
        auto& style = renderer->style();
        auto highlightColor = style.colorResolvingCurrentColor(style.tapHighlightColor());
        if (!node->document().frame()->isMainFrame()) {
            auto* view = node->document().frame()->view();
            for (auto& quad : quads)
                quad = view->contentsToRootView(quad);
        }

        LayoutRoundedRect::Radii borderRadii;
        if (CheckedPtr renderBox = dynamicDowncast<RenderBox>(*renderer))
            borderRadii = renderBox->borderRadii();

        RefPtr element = dynamicDowncast<Element>(*node);
        bool nodeHasBuiltInClickHandling = element && (is<HTMLFormControlElement>(*element) || is<HTMLAnchorElement>(*element) || is<HTMLLabelElement>(*element) || is<HTMLSummaryElement>(*element) || element->isLink());
        send(Messages::WebPageProxy::DidGetTapHighlightGeometries(requestID, highlightColor, quads, roundedIntSize(borderRadii.topLeft()), roundedIntSize(borderRadii.topRight()), roundedIntSize(borderRadii.bottomLeft()), roundedIntSize(borderRadii.bottomRight()), nodeHasBuiltInClickHandling));
    }
#else
    UNUSED_PARAM(requestID);
    UNUSED_PARAM(node);
#endif
}

void WebPage::handleTwoFingerTapAtPoint(const WebCore::IntPoint& point, OptionSet<WebKit::WebEventModifier> modifiers, WebKit::TapIdentifier requestID)
{
    FloatPoint adjustedPoint;
    RefPtr localMainFrame = m_page->localMainFrame();
    Node* nodeRespondingToClick = localMainFrame ? localMainFrame->nodeRespondingToClickEvents(point, adjustedPoint) : nullptr;
    if (!nodeRespondingToClick || !nodeRespondingToClick->renderer()) {
        send(Messages::WebPageProxy::DidNotHandleTapAsClick(roundedIntPoint(adjustedPoint)));
        return;
    }
    sendTapHighlightForNodeIfNecessary(requestID, nodeRespondingToClick, point);
    completeSyntheticClick(std::nullopt, *nodeRespondingToClick, adjustedPoint, modifiers, WebCore::SyntheticClickType::TwoFingerTap);
}

Awaitable<std::optional<WebCore::RemoteUserInputEventData>> WebPage::potentialTapAtPosition(std::optional<WebCore::FrameIdentifier> frameID, WebKit::TapIdentifier requestID, WebCore::FloatPoint position, bool shouldRequestMagnificationInformation)
{
    RefPtr localMainFrame = m_page->localMainFrame();

    if (RefPtr localRootFrame = this->localRootFrame(frameID))
        m_potentialTapNode = localRootFrame->nodeRespondingToClickEvents(position, m_potentialTapLocation, m_potentialTapSecurityOrigin.get());

    RefPtr frameOwner = dynamicDowncast<HTMLFrameOwnerElement>(m_potentialTapNode.get());
    if (RefPtr remoteFrame = frameOwner ? dynamicDowncast<RemoteFrame>(frameOwner->contentFrame()) : nullptr) {
        RefPtr localFrame = frameOwner->document().frame();
        if (RefPtr frameView = localFrame ? localFrame->view() : nullptr) {
            if (RefPtr remoteFrameView = remoteFrame->view()) {
                RemoteFrameGeometryTransformer transformer(remoteFrameView.releaseNonNull(), frameView.releaseNonNull(), remoteFrame->frameID());
                // FIXME: Use a different type with a FloatPoint to avoid rounding to an int.
                co_return WebCore::RemoteUserInputEventData {
                    remoteFrame->frameID(),
                    transformer.transformToRemoteFrameCoordinates(roundedIntPoint(position))
                };
            }
        }
    }

    auto lastTouchLocation = std::exchange(m_lastTouchLocationBeforeTap, { });
    bool ignorePotentialTap = [&] {
        if (!m_potentialTapNode)
            return false;

        if (!localMainFrame)
            return false;

        if (!lastTouchLocation)
            return false;

        static constexpr auto maxAllowedMovementSquared = 200 * 200;
        if ((position - *lastTouchLocation).diagonalLengthSquared() <= maxAllowedMovementSquared)
            return false;

        FloatPoint adjustedLocation;
        RefPtr lastTouchedNode = localMainFrame->nodeRespondingToClickEvents(*lastTouchLocation, adjustedLocation, m_potentialTapSecurityOrigin.get());
        return lastTouchedNode != m_potentialTapNode;
    }();

    if (ignorePotentialTap) {
        // The main frame has scrolled between when the touch started and when the tap gesture fires, such that the hit-tested node underneath
        // the user's touch has changed. Avoid dispatching a synthetic click in this case.
        RELEASE_LOG(ViewGestures, "Ignoring potential tap (distance from last touch: %.0f)", (position - *lastTouchLocation).diagonalLength());
        m_potentialTapNode = nullptr;
        co_return std::nullopt;
    }

    m_wasShowingInputViewForFocusedElementDuringLastPotentialTap = m_isShowingInputViewForFocusedElement;

    RefPtr viewGestureGeometryCollector = m_viewGestureGeometryCollector;

    if (shouldRequestMagnificationInformation && m_potentialTapNode && viewGestureGeometryCollector) {
        // FIXME: Could this be combined into tap highlight?
        FloatPoint origin = position;
        FloatRect absoluteBoundingRect;
        bool fitEntireRect;
        double viewportMinimumScale;
        double viewportMaximumScale;

        viewGestureGeometryCollector->computeZoomInformationForNode(*m_potentialTapNode, origin, absoluteBoundingRect, fitEntireRect, viewportMinimumScale, viewportMaximumScale);

        bool nodeIsRootLevel = is<WebCore::Document>(*m_potentialTapNode) || is<WebCore::HTMLBodyElement>(*m_potentialTapNode);
        bool nodeIsPluginElement = is<WebCore::HTMLPlugInElement>(*m_potentialTapNode);
        // FIXME: This message should become part of the reply.
        send(Messages::WebPageProxy::HandleSmartMagnificationInformationForPotentialTap(requestID, absoluteBoundingRect, fitEntireRect, viewportMinimumScale, viewportMaximumScale, nodeIsRootLevel, nodeIsPluginElement));
    }

    sendTapHighlightForNodeIfNecessary(requestID, m_potentialTapNode.get(), position);
#if ENABLE(TOUCH_EVENTS)
    // FIXME: This message should become part of the reply.
    if (m_potentialTapNode && !m_potentialTapNode->allowsDoubleTapGesture())
        send(Messages::WebPageProxy::DisableDoubleTapGesturesDuringTapIfNecessary(requestID));
#endif
    co_return std::nullopt;
}

Awaitable<std::optional<WebCore::FrameIdentifier>> WebPage::commitPotentialTap(std::optional<WebCore::FrameIdentifier> frameID, OptionSet<WebEventModifier> modifiers, TransactionID lastLayerTreeTransactionId, WebCore::PointerID pointerId)
{
    RefPtr frameOwner = dynamicDowncast<HTMLFrameOwnerElement>(m_potentialTapNode.get());
    RefPtr remoteFrame = frameOwner ? dynamicDowncast<RemoteFrame>(frameOwner->contentFrame()) : nullptr;
    if (remoteFrame)
        co_return remoteFrame->frameID();

    auto invalidTargetForSingleClick = !m_potentialTapNode;
    if (!invalidTargetForSingleClick) {
        bool targetRenders = m_potentialTapNode->renderer();
        if (RefPtr element = dynamicDowncast<Element>(m_potentialTapNode); element && !targetRenders)
            targetRenders = element->renderOrDisplayContentsStyle();
        if (RefPtr shadowRoot = dynamicDowncast<ShadowRoot>(m_potentialTapNode); shadowRoot && !targetRenders)
            targetRenders = shadowRoot->host()->renderOrDisplayContentsStyle();
        invalidTargetForSingleClick = !targetRenders && !is<HTMLAreaElement>(m_potentialTapNode);
    }

    RefPtr localRootFrame = this->localRootFrame(frameID);

    if (invalidTargetForSingleClick) {
        if (localRootFrame) {
            constexpr OptionSet hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
            auto roundedPoint = IntPoint { m_potentialTapLocation };
            auto result = localRootFrame->eventHandler().hitTestResultAtPoint(roundedPoint, hitType);
            localRootFrame->eventHandler().setLastTouchedNode(result.innerNode());
        }

        commitPotentialTapFailed();
        co_return std::nullopt;
    }

    if (localRootFrame)
        localRootFrame->eventHandler().setLastTouchedNode(nullptr);

    FloatPoint adjustedPoint;
    RefPtr nodeRespondingToClick = localRootFrame ? localRootFrame->nodeRespondingToClickEvents(m_potentialTapLocation, adjustedPoint, m_potentialTapSecurityOrigin.get()) : nullptr;
    RefPtr element = dynamicDowncast<Element>(nodeRespondingToClick);
    RefPtr frameRespondingToClick = nodeRespondingToClick ? nodeRespondingToClick->document().frame() : nullptr;

    if (!frameRespondingToClick) {
        commitPotentialTapFailed();
        co_return std::nullopt;
    }

    auto firstTransactionID = WebFrame::fromCoreFrame(*frameRespondingToClick)->firstLayerTreeTransactionIDAfterDidCommitLoad();
    if (firstTransactionID
        && lastLayerTreeTransactionId.processIdentifier() == firstTransactionID->processIdentifier()
        && lastLayerTreeTransactionId.lessThanSameProcess(*firstTransactionID)) {
        commitPotentialTapFailed();
        co_return std::nullopt;
    }

    if (m_potentialTapNode == nodeRespondingToClick)
        handleSyntheticClick(frameID, *nodeRespondingToClick, adjustedPoint, modifiers, pointerId);
    else
        commitPotentialTapFailed();

    m_potentialTapNode = nullptr;
    m_potentialTapLocation = FloatPoint();
    m_potentialTapSecurityOrigin = nullptr;

    co_return std::nullopt;
}

void WebPage::commitPotentialTapFailed()
{
    if (auto selectionChangedHandler = std::exchange(m_selectionChangedHandler, {}))
        selectionChangedHandler();

    if (RefPtr localMainFrame = m_page->localMainFrame())
        ContentChangeObserver::didCancelPotentialTap(*localMainFrame);
    clearSelectionAfterTapIfNeeded();
    invokePendingSyntheticClickCallback(SyntheticClickResult::Failed);

    // FIXME: These two messages should be merged into one, and ideally
    // just sent as part of the IPC reply of WebPage::commitPotentialTap.
    send(Messages::WebPageProxy::CommitPotentialTapFailed());
    send(Messages::WebPageProxy::DidNotHandleTapAsClick(roundedIntPoint(m_potentialTapLocation)));
}

void WebPage::clearSelectionAfterTapIfNeeded()
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = pluginViewForFrame(frame.get())) {
        pluginView->clearSelection();
        return;
    }
#endif

    if (frame->selection().selection().isContentEditable())
        return;

    clearSelection();
}

void WebPage::cancelPotentialTap()
{   
    if (RefPtr localMainFrame = m_page->localMainFrame())
        ContentChangeObserver::didCancelPotentialTap(*localMainFrame);
    cancelPotentialTapInFrame(m_mainFrame);
}

void WebPage::cancelPotentialTapInFrame(WebFrame& frame)
{
    if (auto selectionChangedHandler = std::exchange(m_selectionChangedHandler, {}))
        selectionChangedHandler();

    if (m_potentialTapNode) {
        auto* potentialTapFrame = m_potentialTapNode->document().frame();
        if (potentialTapFrame && !potentialTapFrame->tree().isDescendantOf(frame.coreLocalFrame()))
            return;
    }

    m_potentialTapNode = nullptr;
    m_potentialTapLocation = FloatPoint();
    m_potentialTapSecurityOrigin = nullptr;
}

void WebPage::didRecognizeLongPress()
{
    if (RefPtr localMainFrame = m_page->localMainFrame())
        ContentChangeObserver::didRecognizeLongPress(*localMainFrame);
}

void WebPage::tapHighlightAtPosition(WebKit::TapIdentifier requestID, const FloatPoint& position)
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;
    FloatPoint adjustedPoint;
    sendTapHighlightForNodeIfNecessary(requestID, localMainFrame->nodeRespondingToClickEvents(position, adjustedPoint), position);
}

void WebPage::inspectorNodeSearchMovedToPosition(const FloatPoint& position)
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;
    IntPoint adjustedPoint = roundedIntPoint(position);

    localMainFrame->eventHandler().mouseMoved(PlatformMouseEvent(adjustedPoint, adjustedPoint, MouseButton::None, PlatformEvent::Type::MouseMoved, 0, { }, { }, 0, WebCore::SyntheticClickType::NoTap));
    localMainFrame->document()->updateStyleIfNeeded();
}

void WebPage::inspectorNodeSearchEndedAtPosition(const FloatPoint& position)
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (Node* node = localMainFrame ? localMainFrame->deepestNodeAtLocation(position) : nullptr)
        node->inspect();
}

void WebPage::updateInputContextAfterBlurringAndRefocusingElementIfNeeded(Element& element)
{
    if (m_recentlyBlurredElement != &element || !m_isShowingInputViewForFocusedElement)
        return;

    m_hasPendingInputContextUpdateAfterBlurringAndRefocusingElement = true;
    callOnMainRunLoop([this, protectedThis = Ref { *this }] {
        if (m_hasPendingInputContextUpdateAfterBlurringAndRefocusingElement)
            send(Messages::WebPageProxy::UpdateInputContextAfterBlurringAndRefocusingElement());
        m_hasPendingInputContextUpdateAfterBlurringAndRefocusingElement = false;
    });
}

void WebPage::didProgrammaticallyClearTextFormControl(const HTMLTextFormControlElement& element)
{
    if (!m_isShowingInputViewForFocusedElement)
        return;

    if (m_focusedElement != &element && m_recentlyBlurredElement != &element)
        return;

    callOnMainRunLoop([protectedThis = Ref { *this }, element = Ref { element }] {
        if (protectedThis->m_focusedElement != element.ptr())
            return;

        auto context = protectedThis->contextForElement(element);
        if (!context)
            return;

        protectedThis->send(Messages::WebPageProxy::DidProgrammaticallyClearFocusedElement(WTFMove(*context)));
    });
}

void WebPage::blurFocusedElement()
{
    if (!m_focusedElement)
        return;

    m_focusedElement->blur();
}

void WebPage::setFocusedElementValue(const WebCore::ElementContext& context, const String& value)
{
    // FIXME: should also handle the case of HTMLSelectElement.
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(elementForContext(context)))
        input->setValue(value, DispatchInputAndChangeEvent);
}

void WebPage::setFocusedElementSelectedIndex(const WebCore::ElementContext& context, uint32_t index, bool allowMultipleSelection)
{
    if (RefPtr select = dynamicDowncast<HTMLSelectElement>(elementForContext(context)))
        select->optionSelectedByUser(index, true, allowMultipleSelection);
}

void WebPage::showInspectorHighlight(const WebCore::InspectorOverlay::Highlight& highlight)
{
    send(Messages::WebPageProxy::ShowInspectorHighlight(highlight));
}

void WebPage::hideInspectorHighlight()
{
    send(Messages::WebPageProxy::HideInspectorHighlight());
}

void WebPage::showInspectorIndication()
{
    send(Messages::WebPageProxy::ShowInspectorIndication());
}

void WebPage::hideInspectorIndication()
{
    send(Messages::WebPageProxy::HideInspectorIndication());
}

void WebPage::enableInspectorNodeSearch()
{
    send(Messages::WebPageProxy::EnableInspectorNodeSearch());
}

void WebPage::disableInspectorNodeSearch()
{
    send(Messages::WebPageProxy::DisableInspectorNodeSearch());
}

void WebPage::setForceAlwaysUserScalable(bool userScalable)
{
    m_forceAlwaysUserScalable = userScalable;
    m_viewportConfiguration.setForceAlwaysUserScalable(userScalable);
}

static IntRect elementBoundsInFrame(const LocalFrame& frame, const Element& focusedElement)
{
    frame.document()->updateLayout(LayoutOptions::IgnorePendingStylesheets);
    
    if (focusedElement.hasTagName(HTMLNames::textareaTag) || focusedElement.hasTagName(HTMLNames::inputTag) || focusedElement.hasTagName(HTMLNames::selectTag))
        return WebPage::absoluteInteractionBounds(focusedElement);

    if (auto* rootEditableElement = focusedElement.rootEditableElement())
        return WebPage::absoluteInteractionBounds(*rootEditableElement);

    return { };
}

static IntPoint constrainPoint(const IntPoint& point, const LocalFrame& frame, const Element& focusedElement)
{
    ASSERT(&focusedElement.document() == frame.document());
    const int DEFAULT_CONSTRAIN_INSET = 2;
    IntRect innerFrame = elementBoundsInFrame(frame, focusedElement);
    IntPoint constrainedPoint = point;

    int minX = innerFrame.x() + DEFAULT_CONSTRAIN_INSET;
    int maxX = innerFrame.maxX() - DEFAULT_CONSTRAIN_INSET;
    int minY = innerFrame.y() + DEFAULT_CONSTRAIN_INSET;
    int maxY = innerFrame.maxY() - DEFAULT_CONSTRAIN_INSET;

    if (point.x() < minX)
        constrainedPoint.setX(minX);
    else if (point.x() > maxX)
        constrainedPoint.setX(maxX);

    if (point.y() < minY)
        constrainedPoint.setY(minY);
    else if (point.y() >= maxY)
        constrainedPoint.setY(maxY);
                    
    return constrainedPoint;
}

static bool insideImageOverlay(const VisiblePosition& position)
{
    RefPtr container = position.deepEquivalent().containerNode();
    return container && ImageOverlay::isInsideOverlay(*container);
}

static std::optional<SimpleRange> expandForImageOverlay(const SimpleRange& range)
{
    VisiblePosition expandedStart(makeContainerOffsetPosition(range.protectedStartContainer(), range.startOffset()));
    VisiblePosition expandedEnd(makeContainerOffsetPosition(range.protectedEndContainer(), range.endOffset()));

    for (auto start = expandedStart; insideImageOverlay(start); start = start.previous()) {
        if (RefPtr container = start.deepEquivalent().containerNode(); is<Text>(container)) {
            expandedStart = firstPositionInNode(container.get()).downstream();
            break;
        }
    }

    for (auto end = expandedEnd; insideImageOverlay(end); end = end.next()) {
        if (RefPtr container = end.deepEquivalent().containerNode(); is<Text>(container)) {
            expandedEnd = lastPositionInNode(container.get()).upstream();
            break;
        }
    }

    return makeSimpleRange({ expandedStart, expandedEnd });
}

void WebPage::selectWithGesture(const IntPoint& point, GestureType gestureType, GestureRecognizerState gestureState, bool isInteractingWithFocusedElement, CompletionHandler<void(const WebCore::IntPoint&, GestureType, GestureRecognizerState, OptionSet<SelectionFlags>)>&& completionHandler)
{
    if (static_cast<GestureRecognizerState>(gestureState) == GestureRecognizerState::Began)
        updateFocusBeforeSelectingTextAtLocation(point);

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    VisiblePosition position = visiblePositionInFocusedNodeForPoint(*frame, point, isInteractingWithFocusedElement);

    if (position.isNull()) {
        completionHandler(point, gestureType, gestureState, { });
        return;
    }
    std::optional<SimpleRange> range;
    OptionSet<SelectionFlags> flags;
    GestureRecognizerState wkGestureState = static_cast<GestureRecognizerState>(gestureState);
    switch (static_cast<GestureType>(gestureType)) {
    case GestureType::PhraseBoundary: {
        if (!frame->editor().hasComposition())
            break;
        auto markedRange = frame->editor().compositionRange();
        auto startPosition = VisiblePosition { makeDeprecatedLegacyPosition(markedRange->start) };
        position = std::clamp(position, startPosition, VisiblePosition { makeDeprecatedLegacyPosition(markedRange->end) });
        if (wkGestureState != GestureRecognizerState::Began)
            flags = distanceBetweenPositions(startPosition, frame->selection().selection().start()) != distanceBetweenPositions(startPosition, position) ? SelectionFlags::PhraseBoundaryChanged : OptionSet<SelectionFlags> { };
        else
            flags = SelectionFlags::PhraseBoundaryChanged;
        range = makeSimpleRange(position);
        break;
    }

    case GestureType::OneFingerTap: {
        auto [adjustedPosition, withinWordBoundary] = wordBoundaryForPositionWithoutCrossingLine(position);
        if (withinWordBoundary == WithinWordBoundary::Yes)
            flags = SelectionFlags::WordIsNearTap;
        range = makeSimpleRange(adjustedPosition);
        break;
    }

    case GestureType::Loupe:
        if (position.rootEditableElement())
            range = makeSimpleRange(position);
        else {
#if !PLATFORM(MACCATALYST)
            range = wordRangeFromPosition(position);
#else
            switch (wkGestureState) {
            case GestureRecognizerState::Began:
                m_startingGestureRange = makeSimpleRange(position);
                break;
            case GestureRecognizerState::Changed:
                if (m_startingGestureRange) {
                    auto& start = m_startingGestureRange->start;
                    if (makeDeprecatedLegacyPosition(start) < position)
                        range = makeSimpleRange(start, position);
                    else
                        range = makeSimpleRange(position, start);
                }
                break;
            case GestureRecognizerState::Ended:
            case GestureRecognizerState::Cancelled:
                m_startingGestureRange = std::nullopt;
                break;
            case GestureRecognizerState::Failed:
            case GestureRecognizerState::Possible:
                ASSERT_NOT_REACHED();
                break;
            }
#endif
        }
        break;

    case GestureType::TapAndAHalf:
        switch (wkGestureState) {
        case GestureRecognizerState::Began:
            range = wordRangeFromPosition(position);
            if (range)
                m_currentWordRange = { { *range } };
            else
                m_currentWordRange = std::nullopt;
            break;
        case GestureRecognizerState::Changed:
            if (!m_currentWordRange)
                break;
            range = m_currentWordRange;
            if (position < makeDeprecatedLegacyPosition(range->start))
                range->start = *makeBoundaryPoint(position);
            if (position > makeDeprecatedLegacyPosition(range->end))
                range->end = *makeBoundaryPoint(position);
            break;
        case GestureRecognizerState::Ended:
        case GestureRecognizerState::Cancelled:
            m_currentWordRange = std::nullopt;
            break;
        case GestureRecognizerState::Failed:
        case GestureRecognizerState::Possible:
            ASSERT_NOT_REACHED();
        }
        break;

    case GestureType::OneFingerDoubleTap:
        if (atBoundaryOfGranularity(position, TextGranularity::LineGranularity, SelectionDirection::Forward)) {
            // Double-tap at end of line only places insertion point there.
            // This helps to get the callout for pasting at ends of lines,
            // paragraphs, and documents.
            range = makeSimpleRange(position);
         } else
            range = wordRangeFromPosition(position);
        break;

    case GestureType::TwoFingerSingleTap:
        // Single tap with two fingers selects the entire paragraph.
        range = enclosingTextUnitOfGranularity(position, TextGranularity::ParagraphGranularity, SelectionDirection::Forward);
        break;

    case GestureType::OneFingerTripleTap:
        if (atBoundaryOfGranularity(position, TextGranularity::LineGranularity, SelectionDirection::Forward)) {
            // Triple-tap at end of line only places insertion point there.
            // This helps to get the callout for pasting at ends of lines, paragraphs, and documents.
            range = makeSimpleRange(position);
        } else
            range = enclosingTextUnitOfGranularity(position, TextGranularity::ParagraphGranularity, SelectionDirection::Forward);
        break;

    default:
        break;
    }
    if (range)
        frame->selection().setSelectedRange(range, position.affinity(), WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);

    completionHandler(point, gestureType, gestureState, flags);
}

static std::pair<std::optional<SimpleRange>, SelectionWasFlipped> rangeForPointInRootViewCoordinates(LocalFrame& frame, const IntPoint& pointInRootViewCoordinates, bool baseIsStart, bool selectionFlippingEnabled)
{
    VisibleSelection existingSelection = frame.selection().selection();
    VisiblePosition selectionStart = existingSelection.visibleStart();
    VisiblePosition selectionEnd = existingSelection.visibleEnd();
    RefPtr localMainFrame = frame.localMainFrame();
    if (!localMainFrame)
        return { std::nullopt, SelectionWasFlipped::No };

    auto pointInDocument = frame.view()->rootViewToContents(pointInRootViewCoordinates.constrainedWithin(localMainFrame->view()->unobscuredContentRect()));

    if (!selectionFlippingEnabled) {
        auto node = selectionStart.deepEquivalent().containerNode();
        if (node && node->renderStyle() && node->renderStyle()->writingMode().isVertical()) {
            if (baseIsStart) {
                int startX = selectionStart.absoluteCaretBounds().center().x();
                if (pointInDocument.x() > startX)
                    pointInDocument.setX(startX);
            } else {
                int endX = selectionEnd.absoluteCaretBounds().center().x();
                if (pointInDocument.x() < endX)
                    pointInDocument.setX(endX);
            }
        } else {
            if (baseIsStart) {
                int startY = selectionStart.absoluteCaretBounds().center().y();
                if (pointInDocument.y() < startY)
                    pointInDocument.setY(startY);
            } else {
                int endY = selectionEnd.absoluteCaretBounds().center().y();
                if (pointInDocument.y() > endY)
                    pointInDocument.setY(endY);
            }
        }
    }

    auto hitTest = frame.eventHandler().hitTestResultAtPoint(pointInDocument, {
        HitTestRequest::Type::ReadOnly,
        HitTestRequest::Type::Active,
        HitTestRequest::Type::AllowVisibleChildFrameContentOnly,
    });

    RefPtr targetNode = hitTest.targetNode();
    if (targetNode && !HTMLElement::shouldExtendSelectionToTargetNode(*targetNode, existingSelection))
        return { std::nullopt, SelectionWasFlipped::No };

    VisiblePosition result;
    std::optional<SimpleRange> range;
    SelectionWasFlipped selectionFlipped = SelectionWasFlipped::No;

    auto shouldUseOldExtentAsNewBase = [&] {
        return frame.settings().visuallyContiguousBidiTextSelectionEnabled()
            && crossesBidiTextBoundaryInSameLine(result, baseIsStart ? selectionEnd : selectionStart);
    };

    if (targetNode)
        result = frame.eventHandler().selectionExtentRespectingEditingBoundary(frame.selection().selection(), hitTest.localPoint(), targetNode.get()).deepEquivalent();
    else
        result = frame.visiblePositionForPoint(pointInDocument).deepEquivalent();
    
    if (baseIsStart) {
        bool flipSelection = result < selectionStart;

        if ((flipSelection && !selectionFlippingEnabled) || result == selectionStart)
            result = selectionStart.next();
        else if (RefPtr containerNode = selectionStart.deepEquivalent().containerNode(); containerNode && targetNode && &containerNode->treeScope() != &targetNode->treeScope())
            result = VisibleSelection::adjustPositionForEnd(result.deepEquivalent(), containerNode.get());
        
        if (selectionFlippingEnabled && flipSelection) {
            range = makeSimpleRange(result, shouldUseOldExtentAsNewBase() ? selectionEnd : selectionStart);
            selectionFlipped = SelectionWasFlipped::Yes;
        } else
            range = makeSimpleRange(selectionStart, result);
    } else {
        bool flipSelection = selectionEnd < result;
        
        if ((flipSelection && !selectionFlippingEnabled) || result == selectionEnd)
            result = selectionEnd.previous();
        else if (RefPtr containerNode = selectionEnd.deepEquivalent().containerNode(); containerNode && targetNode && &containerNode->treeScope() != &targetNode->treeScope())
            result = VisibleSelection::adjustPositionForStart(result.deepEquivalent(), containerNode.get());

        if (selectionFlippingEnabled && flipSelection) {
            range = makeSimpleRange(shouldUseOldExtentAsNewBase() ? selectionStart : selectionEnd, result);
            selectionFlipped = SelectionWasFlipped::Yes;
        } else
            range = makeSimpleRange(result, selectionEnd);
    }
    
    if (range && ImageOverlay::isInsideOverlay(*range))
        return { expandForImageOverlay(*range), SelectionWasFlipped::No };

    return { range, selectionFlipped };
}

static std::optional<SimpleRange> rangeAtWordBoundaryForPosition(LocalFrame* frame, const VisiblePosition& position, bool baseIsStart)
{
    SelectionDirection sameDirection = baseIsStart ? SelectionDirection::Forward : SelectionDirection::Backward;
    SelectionDirection oppositeDirection = baseIsStart ? SelectionDirection::Backward : SelectionDirection::Forward;
    VisiblePosition base = baseIsStart ? frame->selection().selection().visibleStart() : frame->selection().selection().visibleEnd();
    VisiblePosition extent = baseIsStart ? frame->selection().selection().visibleEnd() : frame->selection().selection().visibleStart();
    VisiblePosition initialExtent = position;

    if (atBoundaryOfGranularity(extent, TextGranularity::WordGranularity, sameDirection)) {
        // This is a word boundary. Leave selection where it is.
        return std::nullopt;
    }

    if (atBoundaryOfGranularity(extent, TextGranularity::WordGranularity, oppositeDirection)) {
        // This is a word boundary in the wrong direction. Nudge the selection to a character before proceeding.
        extent = baseIsStart ? extent.previous() : extent.next();
    }

    // Extend to the boundary of the word.

    VisiblePosition wordBoundary = positionOfNextBoundaryOfGranularity(extent, TextGranularity::WordGranularity, sameDirection);
    if (wordBoundary.isNotNull()
        && atBoundaryOfGranularity(wordBoundary, TextGranularity::WordGranularity, sameDirection)
        && initialExtent != wordBoundary) {
        extent = wordBoundary;
        if (!(base < extent))
            std::swap(base, extent);
        return makeSimpleRange(base, extent);
    }
    // Conversely, if the initial extent equals the current word boundary, then
    // run the rest of this function to see if the selection should extend
    // the other direction to the other word.

    // If this is where the extent was initially, then iterate in the other direction in the document until we hit the next word.
    while (extent.isNotNull()
        && !atBoundaryOfGranularity(extent, TextGranularity::WordGranularity, sameDirection)
        && extent != base
        && !atBoundaryOfGranularity(extent, TextGranularity::LineGranularity, sameDirection)
        && !atBoundaryOfGranularity(extent, TextGranularity::LineGranularity, oppositeDirection)) {
        extent = baseIsStart ? extent.next() : extent.previous();
    }

    // Don't let the smart extension make the extent equal the base.
    // Expand out to word boundary.
    if (extent.isNull() || extent == base)
        extent = wordBoundary;
    if (extent.isNull())
        return std::nullopt;

    if (!(base < extent))
        std::swap(base, extent);
    return makeSimpleRange(base, extent);
}

IntRect WebPage::rootViewBounds(const Node& node)
{
    RefPtr frame = node.document().frame();
    if (!frame)
        return { };

    RefPtr view = frame->view();
    if (!view)
        return { };

    auto* renderer = node.renderer();
    if (!renderer)
        return { };

    return view->contentsToRootView(renderer->absoluteBoundingBoxRect());
}

IntRect WebPage::absoluteInteractionBounds(const Node& node)
{
    RefPtr frame = node.document().frame();
    if (!frame)
        return { };

    RefPtr view = frame->view();
    if (!view)
        return { };

    auto* renderer = node.renderer();
    if (!renderer)
        return { };

    if (CheckedPtr box = dynamicDowncast<RenderBox>(*renderer)) {
        FloatRect rect;
        // FIXME: want borders or not?
        if (box->style().isOverflowVisible())
            rect = box->layoutOverflowRect();
        else
            rect = box->clientBoxRect();
        return box->localToAbsoluteQuad(rect).enclosingBoundingBox();
    }

    auto& style = renderer->style();
    FloatRect boundingBox = renderer->absoluteBoundingBoxRect(true /* use transforms*/);
    // This is wrong. It's subtracting borders after converting to absolute coords on something that probably doesn't represent a rectangular element.
    boundingBox.move(style.borderLeftWidth(), style.borderTopWidth());
    boundingBox.setWidth(boundingBox.width() - style.borderLeftWidth() - style.borderRightWidth());
    boundingBox.setHeight(boundingBox.height() - style.borderBottomWidth() - style.borderTopWidth());
    return enclosingIntRect(boundingBox);
}

IntRect WebPage::rootViewInteractionBounds(const Node& node)
{
    RefPtr frame = node.document().frame();
    if (!frame)
        return { };

    RefPtr view = frame->view();
    if (!view)
        return { };

    return view->contentsToRootView(absoluteInteractionBounds(node));
}

void WebPage::clearSelection()
{
    m_startingGestureRange = std::nullopt;
    RefPtr focusedOrMainFrame = m_page->focusController().focusedOrMainFrame();
    focusedOrMainFrame->selection().clear();
#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = pluginViewForFrame(focusedOrMainFrame.get()))
        pluginView->clearSelection();
#endif
}

void WebPage::dispatchSyntheticMouseEventsForSelectionGesture(SelectionTouch touch, const IntPoint& point)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    if (!frame->selection().selection().isContentEditable())
        return;

    IntRect focusedElementRect;
    if (m_focusedElement)
        focusedElementRect = rootViewInteractionBounds(*m_focusedElement);

    if (focusedElementRect.isEmpty())
        return;

    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;

    auto adjustedPoint = point.constrainedBetween(focusedElementRect.minXMinYCorner(), focusedElementRect.maxXMaxYCorner());
    auto& eventHandler = localMainFrame->eventHandler();
    switch (touch) {
    case SelectionTouch::Started:
        eventHandler.handleMousePressEvent({ adjustedPoint, adjustedPoint, MouseButton::Left, PlatformEvent::Type::MousePressed, 1, { }, WallTime::now(), WebCore::ForceAtClick, WebCore::SyntheticClickType::NoTap });
        break;
    case SelectionTouch::Moved:
        eventHandler.dispatchSyntheticMouseMove({ adjustedPoint, adjustedPoint, MouseButton::Left, PlatformEvent::Type::MouseMoved, 0, { }, WallTime::now(), WebCore::ForceAtClick, WebCore::SyntheticClickType::NoTap });
        break;
    case SelectionTouch::Ended:
    case SelectionTouch::EndedMovingForward:
    case SelectionTouch::EndedMovingBackward:
    case SelectionTouch::EndedNotMoving:
        eventHandler.handleMouseReleaseEvent({ adjustedPoint, adjustedPoint, MouseButton::Left, PlatformEvent::Type::MouseReleased, 1, { }, WallTime::now(), WebCore::ForceAtClick, WebCore::SyntheticClickType::NoTap });
        break;
    }
}

void WebPage::clearSelectionAfterTappingSelectionHighlightIfNeeded(WebCore::FloatPoint location)
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;

    auto result = localMainFrame->eventHandler().hitTestResultAtPoint(LayoutPoint { location }, {
        HitTestRequest::Type::ReadOnly,
        HitTestRequest::Type::AllowVisibleChildFrameContentOnly,
        HitTestRequest::Type::IncludeAllElementsUnderPoint,
        HitTestRequest::Type::CollectMultipleElements,
    });

    bool tappedVideo = false;
    bool tappedLiveText = false;
    for (Ref node : result.listBasedTestResult()) {
        if (is<HTMLVideoElement>(node))
            tappedVideo = true;
        else if (ImageOverlay::isOverlayText(node))
            tappedLiveText = true;
    }

    if (tappedVideo && !tappedLiveText)
        clearSelection();
}

bool WebPage::shouldDrawVisuallyContiguousBidiSelection() const
{
    return m_page->settings().visuallyContiguousBidiTextSelectionEnabled() && m_activeTextInteractionSources;
}

void WebPage::addTextInteractionSources(OptionSet<TextInteractionSource> sources)
{
    if (sources.isEmpty())
        return;

    bool wasEmpty = m_activeTextInteractionSources.isEmpty();
    m_activeTextInteractionSources.add(sources);
    if (!wasEmpty)
        return;

    if (m_page->settings().visuallyContiguousBidiTextSelectionEnabled())
        scheduleFullEditorStateUpdate();
}

void WebPage::removeTextInteractionSources(OptionSet<TextInteractionSource> sources)
{
    if (m_activeTextInteractionSources.isEmpty())
        return;

    m_activeTextInteractionSources.remove(sources);

    if (!m_activeTextInteractionSources.isEmpty())
        return;

    if (!m_page->settings().visuallyContiguousBidiTextSelectionEnabled())
        return;

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    auto originalSelection = frame->selection().selection();
    if (!originalSelection.isNonOrphanedRange())
        return;

    auto originalSelectedRange = originalSelection.toNormalizedRange();
    if (!originalSelectedRange)
        return;

    auto adjustedRange = adjustToVisuallyContiguousRange(*originalSelectedRange);
    if (originalSelectedRange == adjustedRange)
        return;

    frame->selection().setSelectedRange(adjustedRange, originalSelection.affinity(), FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
}

void WebPage::updateSelectionWithTouches(const IntPoint& point, SelectionTouch selectionTouch, bool baseIsStart, CompletionHandler<void(const WebCore::IntPoint&, SelectionTouch, OptionSet<SelectionFlags>)>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler(point, selectionTouch, { });

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = focusedPluginViewForFrame(*frame)) {
        OptionSet<SelectionFlags> resultFlags;
        auto startOrEnd = baseIsStart ? SelectionEndpoint::End : SelectionEndpoint::Start;
        if (pluginView->moveSelectionEndpoint(point, startOrEnd) == SelectionWasFlipped::Yes)
            resultFlags.add(SelectionFlags::SelectionFlipped);
        return completionHandler(point, selectionTouch, resultFlags);
    }
#endif

    if (selectionTouch == SelectionTouch::Moved
        && m_bidiSelectionFlippingState != BidiSelectionFlippingState::NotFlipping
        && baseIsStart == (m_bidiSelectionFlippingState == BidiSelectionFlippingState::FlippingToStart)) {
        // The last selection update caused the selection base and extent to swap. Ignore any in-flight selection
        // update that's still trying to adjust the new selection base (after flipping).
        return completionHandler(point, selectionTouch, { });
    }

    if (selectionTouch == SelectionTouch::Started)
        addTextInteractionSources(TextInteractionSource::Touch);

    IntPoint pointInDocument = RefPtr(frame->view())->rootViewToContents(point);
    VisiblePosition position = frame->visiblePositionForPoint(pointInDocument);
    if (position.isNull())
        return completionHandler(point, selectionTouch, { });

    if (shouldDispatchSyntheticMouseEventsWhenModifyingSelection())
        dispatchSyntheticMouseEventsForSelectionGesture(selectionTouch, point);

    std::optional<SimpleRange> range;
    OptionSet<SelectionFlags> flags;
    SelectionWasFlipped selectionFlipped = SelectionWasFlipped::No;

    switch (selectionTouch) {
    case SelectionTouch::Started:
    case SelectionTouch::EndedNotMoving:
        break;

    case SelectionTouch::Ended:
        if (frame->selection().selection().isContentEditable())
            range = makeSimpleRange(closestWordBoundaryForPosition(position));
        else
            std::tie(range, selectionFlipped) = rangeForPointInRootViewCoordinates(*frame, point, baseIsStart, selectionFlippingEnabled());
        break;

    case SelectionTouch::EndedMovingForward:
    case SelectionTouch::EndedMovingBackward:
        range = rangeAtWordBoundaryForPosition(frame.get(), position, baseIsStart);
        break;

    case SelectionTouch::Moved:
        std::tie(range, selectionFlipped) = rangeForPointInRootViewCoordinates(*frame, point, baseIsStart, selectionFlippingEnabled());
        break;
    }

    if (range)
        frame->selection().setSelectedRange(range, position.affinity(), WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);

    switch (selectionTouch) {
    case SelectionTouch::Started:
    case SelectionTouch::Moved:
        break;
    case SelectionTouch::Ended:
    case SelectionTouch::EndedMovingForward:
    case SelectionTouch::EndedMovingBackward:
    case SelectionTouch::EndedNotMoving:
        removeTextInteractionSources(TextInteractionSource::Touch);
        break;
    }

    if (selectionFlipped == SelectionWasFlipped::Yes) {
        flags.add(SelectionFlags::SelectionFlipped);
        m_bidiSelectionFlippingState = baseIsStart ? BidiSelectionFlippingState::FlippingToStart : BidiSelectionFlippingState::FlippingToEnd;
    } else
        m_bidiSelectionFlippingState = BidiSelectionFlippingState::NotFlipping;

    completionHandler(point, selectionTouch, flags);
}

void WebPage::selectWithTwoTouches(const WebCore::IntPoint& from, const WebCore::IntPoint& to, GestureType gestureType, GestureRecognizerState gestureState, CompletionHandler<void(const WebCore::IntPoint&, GestureType, GestureRecognizerState, OptionSet<SelectionFlags>)>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    RefPtr view = frame->view();
    auto fromPosition = frame->visiblePositionForPoint(view->rootViewToContents(from));
    auto toPosition = frame->visiblePositionForPoint(view->rootViewToContents(to));
    if (auto range = makeSimpleRange(fromPosition, toPosition)) {
        if (!(fromPosition < toPosition))
            std::swap(range->start, range->end);
        frame->selection().setSelectedRange(range, fromPosition.affinity(), WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
    }

    // We can use the same callback for the gestures with one point.
    completionHandler(from, gestureType, gestureState, { });
}

void WebPage::extendSelectionForReplacement(CompletionHandler<void()>&& completion)
{
    auto callCompletionHandlerOnExit = makeScopeExit(WTFMove(completion));

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    RefPtr document = frame->document();
    if (!document)
        return;

    auto selectedRange = frame->selection().selection().range();
    if (!selectedRange || !selectedRange->collapsed())
        return;

    VisiblePosition position = frame->selection().selection().start();
    RefPtr container = position.deepEquivalent().containerNode();
    if (!container)
        return;

    auto markerRanges = document->markers().markersFor(*container, { DocumentMarkerType::DictationAlternatives, DocumentMarkerType::CorrectionIndicator }).map([&](auto& marker) {
        return makeSimpleRange(*container, *marker);
    });

    std::optional<SimpleRange> rangeToSelect;
    for (auto& markerRange : markerRanges) {
        if (contains(makeVisiblePositionRange(markerRange), position)) {
            // In practice, dictation markers should only span a single text node, so it's sufficient to
            // grab the first matching range (instead of taking the union of all intersecting ranges).
            rangeToSelect = markerRange;
            break;
        }
    }

    if (!rangeToSelect)
        rangeToSelect = wordRangeFromPosition(position);

    if (!rangeToSelect)
        return;

    if (auto lastRange = std::exchange(m_lastSelectedReplacementRange, { }); lastRange && makeSimpleRange(*lastRange) == rangeToSelect)
        return;

    setSelectedRangeDispatchingSyntheticMouseEventsIfNeeded(*rangeToSelect, position.affinity());
    m_lastSelectedReplacementRange = rangeToSelect->makeWeakSimpleRange();
}

void WebPage::resetLastSelectedReplacementRangeIfNeeded()
{
    if (!m_lastSelectedReplacementRange)
        return;

    auto replacementRange = makeSimpleRange(*m_lastSelectedReplacementRange);
    if (!replacementRange) {
        m_lastSelectedReplacementRange = { };
        return;
    }

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame) {
        m_lastSelectedReplacementRange = { };
        return;
    }

    auto selectedRange = frame->selection().selection().toNormalizedRange();
    if (!selectedRange) {
        m_lastSelectedReplacementRange = { };
        return;
    }

    if (!contains(ComposedTree, *replacementRange, *selectedRange))
        m_lastSelectedReplacementRange = { };
}

void WebPage::extendSelection(WebCore::TextGranularity granularity, CompletionHandler<void()>&& completionHandler)
{
    auto callCompletionHandlerOnExit = makeScopeExit(WTFMove(completionHandler));

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    // For the moment we handle only TextGranularity::WordGranularity.
    if (granularity != TextGranularity::WordGranularity || !frame->selection().isCaret())
        return;

    VisiblePosition position = frame->selection().selection().start();
    auto wordRange = wordRangeFromPosition(position);
    if (!wordRange)
        return;

    setSelectedRangeDispatchingSyntheticMouseEventsIfNeeded(*wordRange, position.affinity());
}

void WebPage::setSelectedRangeDispatchingSyntheticMouseEventsIfNeeded(const SimpleRange& range, Affinity affinity)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    IntPoint endLocationForSyntheticMouseEvents;
    bool shouldDispatchMouseEvents = shouldDispatchSyntheticMouseEventsWhenModifyingSelection();
    if (shouldDispatchMouseEvents) {
        RefPtr view = frame->view();
        auto startLocationForSyntheticMouseEvents = view->contentsToRootView(VisiblePosition(makeDeprecatedLegacyPosition(range.start)).absoluteCaretBounds()).center();
        endLocationForSyntheticMouseEvents = view->contentsToRootView(VisiblePosition(makeDeprecatedLegacyPosition(range.end)).absoluteCaretBounds()).center();
        dispatchSyntheticMouseEventsForSelectionGesture(SelectionTouch::Started, startLocationForSyntheticMouseEvents);
        dispatchSyntheticMouseEventsForSelectionGesture(SelectionTouch::Moved, endLocationForSyntheticMouseEvents);
    }

    frame->selection().setSelectedRange(range, affinity, WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);

    if (shouldDispatchMouseEvents)
        dispatchSyntheticMouseEventsForSelectionGesture(SelectionTouch::Ended, endLocationForSyntheticMouseEvents);
}

void WebPage::platformDidSelectAll()
{
    if (!shouldDispatchSyntheticMouseEventsWhenModifyingSelection())
        return;

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    RefPtr view = frame->view();
    auto startCaretRect = view->contentsToRootView(VisiblePosition(frame->selection().selection().start()).absoluteCaretBounds());
    auto endCaretRect = view->contentsToRootView(VisiblePosition(frame->selection().selection().end()).absoluteCaretBounds());
    dispatchSyntheticMouseEventsForSelectionGesture(SelectionTouch::Started, startCaretRect.center());
    dispatchSyntheticMouseEventsForSelectionGesture(SelectionTouch::Moved, endCaretRect.center());
    dispatchSyntheticMouseEventsForSelectionGesture(SelectionTouch::Ended, endCaretRect.center());
}

void WebPage::selectWordBackward()
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    if (!frame->selection().isCaret())
        return;

    auto position = frame->selection().selection().visibleStart();
    auto startPosition = positionOfNextBoundaryOfGranularity(position, TextGranularity::WordGranularity, SelectionDirection::Backward);
    if (startPosition.isNull() || startPosition == position)
        return;

    frame->selection().setSelectedRange(makeSimpleRange(startPosition, position), position.affinity(), WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
}

void WebPage::moveSelectionByOffset(int32_t offset, CompletionHandler<void()>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;
    
    VisiblePosition startPosition = frame->selection().selection().end();
    if (startPosition.isNull())
        return;
    SelectionDirection direction = offset < 0 ? SelectionDirection::Backward : SelectionDirection::Forward;
    VisiblePosition position = startPosition;
    for (int i = 0; i < std::abs(offset); ++i) {
        position = positionOfNextBoundaryOfGranularity(position, TextGranularity::CharacterGranularity, direction);
        if (position.isNull())
            break;
    }
    if (position.isNotNull() && startPosition != position)
        frame->selection().setSelectedRange(makeSimpleRange(position), position.affinity(), WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
    completionHandler();
}
    
void WebPage::startAutoscrollAtPosition(const WebCore::FloatPoint& positionInWindow)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    if (m_focusedElement && m_focusedElement->renderer()) {
        frame->eventHandler().startSelectionAutoscroll(m_focusedElement->renderer(), positionInWindow);
        return;
    }

    auto& selection = frame->selection().selection();
    if (!selection.isRange())
        return;

    auto range = selection.toNormalizedRange();
    if (!range)
        return;

    auto* renderer = range->start.container->renderer();
    if (!renderer)
        return;

    frame->eventHandler().startSelectionAutoscroll(renderer, positionInWindow);
}
    
void WebPage::cancelAutoscroll()
{
    if (RefPtr frame = m_page->focusController().focusedOrMainFrame())
        frame->eventHandler().cancelSelectionAutoscroll();
}

void WebPage::requestEvasionRectsAboveSelection(CompletionHandler<void(const Vector<FloatRect>&)>&& reply)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return reply({ });

    RefPtr frameView = frame->view();
    if (!frameView) {
        reply({ });
        return;
    }

    auto selection = frame->selection().selection();
    if (selection.isNone()) {
        reply({ });
        return;
    }

    auto selectedRange = selection.toNormalizedRange();
    if (!selectedRange) {
        reply({ });
        return;
    }

    if (!m_focusedElement || !m_focusedElement->renderer() || isTransparentOrFullyClipped(*m_focusedElement)) {
        reply({ });
        return;
    }

    float scaleFactor = pageScaleFactor();
    const double factorOfContentArea = 0.5;
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame) {
        reply({ });
        return;
    }

    auto unobscuredContentArea = RefPtr(localMainFrame->view())->unobscuredContentRect().area();
    if (unobscuredContentArea.hasOverflowed()) {
        reply({ });
        return;
    }

    double contextMenuAreaLimit = factorOfContentArea * scaleFactor * unobscuredContentArea.value();

    FloatRect selectionBoundsInRootViewCoordinates;
    if (selection.isRange())
        selectionBoundsInRootViewCoordinates = frameView->contentsToRootView(unionRect(RenderObject::absoluteTextRects(*selectedRange)));
    else
        selectionBoundsInRootViewCoordinates = frameView->contentsToRootView(frame->selection().absoluteCaretBounds());

    auto centerOfTargetBounds = selectionBoundsInRootViewCoordinates.center();
    FloatPoint centerTopInRootViewCoordinates { centerOfTargetBounds.x(), selectionBoundsInRootViewCoordinates.y() };

    auto clickableNonEditableNode = [&] (const FloatPoint& locationInRootViewCoordinates) -> Node* {
        RefPtr localMainFrame = m_page->localMainFrame();
        if (!localMainFrame)
            return nullptr;

        FloatPoint adjustedPoint;
        auto* hitNode = Ref(*localMainFrame)->nodeRespondingToClickEvents(locationInRootViewCoordinates, adjustedPoint);
        if (!hitNode || is<HTMLBodyElement>(hitNode) || is<Document>(hitNode) || hitNode->hasEditableStyle())
            return nullptr;

        return hitNode;
    };

    // This heuristic attempts to find a list of rects to avoid when showing the callout menu on iOS.
    // First, hit-test several points above the bounds of the selection rect in search of clickable nodes that are not editable.
    // Secondly, hit-test several points around the edges of the selection rect and exclude any nodes found in the first round of
    // hit-testing if these nodes are also reachable by moving outwards from the left, right, or bottom edges of the selection.
    // Additionally, exclude any hit-tested nodes that are either very large relative to the size of the root view, or completely
    // encompass the selection bounds. The resulting rects are the bounds of these hit-tested nodes in root view coordinates.
    HashSet<Ref<Node>> hitTestedNodes;
    Vector<FloatRect> rectsToAvoidInRootViewCoordinates;
    const Vector<FloatPoint, 5> offsetsForHitTesting { { -30, -50 }, { 30, -50 }, { -60, -35 }, { 60, -35 }, { 0, -20 } };
    for (auto offset : offsetsForHitTesting) {
        offset.scale(1 / scaleFactor);
        if (auto* hitNode = clickableNonEditableNode(centerTopInRootViewCoordinates + offset))
            hitTestedNodes.add(*hitNode);
    }

    const float marginForHitTestingSurroundingNodes = 80 / scaleFactor;
    Vector<FloatPoint, 3> exclusionHitTestLocations {
        { selectionBoundsInRootViewCoordinates.x() - marginForHitTestingSurroundingNodes, centerOfTargetBounds.y() },
        { centerOfTargetBounds.x(), selectionBoundsInRootViewCoordinates.maxY() + marginForHitTestingSurroundingNodes },
        { selectionBoundsInRootViewCoordinates.maxX() + marginForHitTestingSurroundingNodes, centerOfTargetBounds.y() }
    };

    for (auto& location : exclusionHitTestLocations) {
        if (auto* nodeToExclude = clickableNonEditableNode(location))
            hitTestedNodes.remove(*nodeToExclude);
    }

    for (auto& node : hitTestedNodes) {
        RefPtr frameView = node->document().view();
        auto* renderer = node->renderer();
        if (!renderer || !frameView)
            continue;

        auto bounds = frameView->contentsToRootView(renderer->absoluteBoundingBoxRect());
        auto area = bounds.area();
        if (area.hasOverflowed() || area > contextMenuAreaLimit)
            continue;

        if (bounds.contains(enclosingIntRect(selectionBoundsInRootViewCoordinates)))
            continue;

        rectsToAvoidInRootViewCoordinates.append(WTFMove(bounds));
    }

    reply(WTFMove(rectsToAvoidInRootViewCoordinates));
}

void WebPage::getRectsForGranularityWithSelectionOffset(WebCore::TextGranularity granularity, int32_t offset, CompletionHandler<void(const Vector<WebCore::SelectionGeometry>&)>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler({ });

    auto selection = m_internals->storedSelectionForAccessibility.isNone() ? frame->selection().selection() : m_internals->storedSelectionForAccessibility;
    auto position = visiblePositionForPositionWithOffset(selection.visibleStart(), offset);
    auto direction = offset < 0 ? SelectionDirection::Backward : SelectionDirection::Forward;
    auto range = enclosingTextUnitOfGranularity(position, granularity, direction);
    if (!range || range->collapsed()) {
        completionHandler({ });
        return;
    }

    auto selectionGeometries = RenderObject::collectSelectionGeometriesWithoutUnionInteriorLines(*range);
    RefPtr view = frame->view();
    convertContentToRootView(*view, selectionGeometries);
    completionHandler(selectionGeometries);
}

void WebPage::storeSelectionForAccessibility(bool shouldStore)
{
    if (!shouldStore)
        m_internals->storedSelectionForAccessibility = VisibleSelection();
    else {
        if (RefPtr frame = m_page->focusController().focusedOrMainFrame())
            m_internals->storedSelectionForAccessibility = frame->selection().selection();
    }
}

static std::optional<SimpleRange> rangeNearPositionMatchesText(const VisiblePosition& position, const String& matchText, const VisibleSelection& selection)
{
    auto liveRange = selection.firstRange();
    if (!liveRange)
        return std::nullopt;
    SimpleRange range { *liveRange };
    auto boundaryPoint = makeBoundaryPoint(position);
    if (!boundaryPoint)
        return std::nullopt;
    return findClosestPlainText(range, matchText, { }, characterCount({ range.start, *boundaryPoint }, TextIteratorBehavior::EmitsCharactersBetweenAllVisiblePositions));
}

void WebPage::getRectsAtSelectionOffsetWithText(int32_t offset, const String& text, CompletionHandler<void(const Vector<WebCore::SelectionGeometry>&)>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler({ });
    auto& selection = m_internals->storedSelectionForAccessibility.isNone() ? frame->selection().selection() : m_internals->storedSelectionForAccessibility;
    auto startPosition = visiblePositionForPositionWithOffset(selection.visibleStart(), offset);
    auto range = makeSimpleRange(startPosition, visiblePositionForPositionWithOffset(startPosition, text.length()));
    if (!range || range->collapsed()) {
        completionHandler({ });
        return;
    }

    if (plainTextForDisplay(*range) != text) {
        // Try to search for a range which is the closest to the position within the selection range that matches the passed in text.
        if (auto wordRange = rangeNearPositionMatchesText(startPosition, text, selection)) {
            if (!wordRange->collapsed())
                range = *wordRange;
        }
    }

    auto selectionGeometries = RenderObject::collectSelectionGeometriesWithoutUnionInteriorLines(*range);
    RefPtr view = frame->view();
    convertContentToRootView(*view, selectionGeometries);
    completionHandler(selectionGeometries);
}

VisiblePosition WebPage::visiblePositionInFocusedNodeForPoint(const LocalFrame& frame, const IntPoint& point, bool isInteractingWithFocusedElement)
{
    IntPoint adjustedPoint(frame.view()->rootViewToContents(point));
    IntPoint constrainedPoint = m_focusedElement && isInteractingWithFocusedElement ? constrainPoint(adjustedPoint, frame, *m_focusedElement) : adjustedPoint;
    return frame.visiblePositionForPoint(constrainedPoint);
}

void WebPage::selectPositionAtPoint(const WebCore::IntPoint& point, bool isInteractingWithFocusedElement, CompletionHandler<void()>&& completionHandler)
{
    SetForScope userIsInteractingChange { m_userIsInteracting, true };

    updateFocusBeforeSelectingTextAtLocation(point);

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler();

    VisiblePosition position = visiblePositionInFocusedNodeForPoint(*frame, point, isInteractingWithFocusedElement);
    
    if (position.isNotNull())
        frame->selection().setSelectedRange(makeSimpleRange(position), position.affinity(), WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
    completionHandler();
}

void WebPage::selectPositionAtBoundaryWithDirection(const WebCore::IntPoint& point, WebCore::TextGranularity granularity, WebCore::SelectionDirection direction, bool isInteractingWithFocusedElement, CompletionHandler<void()>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler();

    VisiblePosition position = visiblePositionInFocusedNodeForPoint(*frame, point, isInteractingWithFocusedElement);

    if (position.isNotNull()) {
        position = positionOfNextBoundaryOfGranularity(position, granularity, direction);
        if (position.isNotNull())
            frame->selection().setSelectedRange(makeSimpleRange(position), Affinity::Upstream, WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
    }
    completionHandler();
}

void WebPage::moveSelectionAtBoundaryWithDirection(WebCore::TextGranularity granularity, WebCore::SelectionDirection direction, CompletionHandler<void()>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler();

    if (!frame->selection().selection().isNone()) {
        bool isForward = (direction == SelectionDirection::Forward || direction == SelectionDirection::Right);
        VisiblePosition position = (isForward) ? frame->selection().selection().visibleEnd() : frame->selection().selection().visibleStart();
        position = positionOfNextBoundaryOfGranularity(position, granularity, direction);
        if (position.isNotNull())
            frame->selection().setSelectedRange(makeSimpleRange(position), isForward? Affinity::Upstream : Affinity::Downstream, WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
    }
    completionHandler();
}

std::optional<SimpleRange> WebPage::rangeForGranularityAtPoint(LocalFrame& frame, const WebCore::IntPoint& point, WebCore::TextGranularity granularity, bool isInteractingWithFocusedElement)
{
    auto position = visiblePositionInFocusedNodeForPoint(frame, point, isInteractingWithFocusedElement);
    switch (granularity) {
    case TextGranularity::CharacterGranularity:
        return makeSimpleRange(position);
    case TextGranularity::WordGranularity:
        return wordRangeFromPosition(position);
    case TextGranularity::SentenceGranularity:
    case TextGranularity::ParagraphGranularity:
        return enclosingTextUnitOfGranularity(position, granularity, SelectionDirection::Forward);
    case TextGranularity::DocumentGranularity:
        // FIXME: Makes no sense that this mutates the current selection and returns null.
        frame.selection().selectAll();
        return std::nullopt;
    case TextGranularity::LineGranularity:
        ASSERT_NOT_REACHED();
        return std::nullopt;
    case TextGranularity::LineBoundary:
        ASSERT_NOT_REACHED();
        return std::nullopt;
    case TextGranularity::SentenceBoundary:
        ASSERT_NOT_REACHED();
        return std::nullopt;
    case TextGranularity::ParagraphBoundary:
        ASSERT_NOT_REACHED();
        return std::nullopt;
    case TextGranularity::DocumentBoundary:
        ASSERT_NOT_REACHED();
        return std::nullopt;
    }
    ASSERT_NOT_REACHED();
    return std::nullopt;
}

static inline bool rectIsTooBigForSelection(const IntRect& blockRect, const LocalFrame& frame)
{
    const float factor = 0.97;
    return blockRect.height() > frame.view()->unobscuredContentRect().height() * factor;
}

void WebPage::updateFocusBeforeSelectingTextAtLocation(const IntPoint& point)
{
    static constexpr OptionSet hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;

    auto result = localMainFrame->eventHandler().hitTestResultAtPoint(point, hitType);
    RefPtr hitNode = result.innerNode();
    if (!hitNode || !hitNode->renderer())
        return;

    RefPtr frame = result.innerNodeFrame();
    m_page->focusController().setFocusedFrame(frame.get());

    if (!result.isOverWidget())
        return;

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = pluginViewForFrame(frame.get()))
        pluginView->focusPluginElement();
#endif
}

void WebPage::setSelectionRange(const WebCore::IntPoint& point, WebCore::TextGranularity granularity, bool isInteractingWithFocusedElement)
{
    updateFocusBeforeSelectingTextAtLocation(point);

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

#if ENABLE(PDF_PLUGIN)
    // FIXME: Support text selection in embedded PDFs.
    if (RefPtr pluginView = focusedPluginViewForFrame(*frame)) {
        pluginView->setSelectionRange(point, granularity);
        return;
    }
#endif

    auto range = rangeForGranularityAtPoint(*frame, point, granularity, isInteractingWithFocusedElement);
    if (range)
        frame->selection().setSelectedRange(*range, Affinity::Upstream, WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);

    m_initialSelection = range;
}

void WebPage::selectTextWithGranularityAtPoint(const WebCore::IntPoint& point, WebCore::TextGranularity granularity, bool isInteractingWithFocusedElement, CompletionHandler<void()>&& completionHandler)
{
    if (!m_potentialTapNode) {
        setSelectionRange(point, granularity, isInteractingWithFocusedElement);
        completionHandler();
        return;
    }
    
    ASSERT(!m_selectionChangedHandler);
    if (auto selectionChangedHandler = std::exchange(m_selectionChangedHandler, {}))
        selectionChangedHandler();

    m_selectionChangedHandler = [point, granularity, isInteractingWithFocusedElement, completionHandler = WTFMove(completionHandler), webPage = WeakPtr { *this }, this]() mutable {
        RefPtr<WebPage> strongPage = webPage.get();
        if (!strongPage) {
            completionHandler();
            return;
        }
        setSelectionRange(point, granularity, isInteractingWithFocusedElement);
        completionHandler();
    };

}

void WebPage::beginSelectionInDirection(WebCore::SelectionDirection direction, CompletionHandler<void(bool)>&& completionHandler)
{
    m_selectionAnchor = direction == SelectionDirection::Left ? SelectionAnchor::Start : SelectionAnchor::End;
    completionHandler(m_selectionAnchor == SelectionAnchor::Start);
}

void WebPage::updateSelectionWithExtentPointAndBoundary(const WebCore::IntPoint& point, WebCore::TextGranularity granularity, bool isInteractingWithFocusedElement, TextInteractionSource source, CompletionHandler<void(bool)>&& callback)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return callback(false);

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = focusedPluginViewForFrame(*frame)) {
        auto movedEndpoint = pluginView->extendInitialSelection(point, granularity);
        return callback(movedEndpoint == SelectionEndpoint::End);
    }
#endif

    auto position = visiblePositionInFocusedNodeForPoint(*frame, point, isInteractingWithFocusedElement);
    auto newRange = rangeForGranularityAtPoint(*frame, point, granularity, isInteractingWithFocusedElement);
    
    if (position.isNull() || !m_initialSelection || !newRange)
        return callback(false);

    addTextInteractionSources(source);

    auto initialSelectionStartPosition = makeDeprecatedLegacyPosition(m_initialSelection->start);
    auto initialSelectionEndPosition = makeDeprecatedLegacyPosition(m_initialSelection->end);

    VisiblePosition selectionStart = initialSelectionStartPosition;
    VisiblePosition selectionEnd = initialSelectionEndPosition;
    if (position > initialSelectionEndPosition)
        selectionEnd = makeDeprecatedLegacyPosition(newRange->end);
    else if (position < initialSelectionStartPosition)
        selectionStart = makeDeprecatedLegacyPosition(newRange->start);

    if (auto range = makeSimpleRange(selectionStart, selectionEnd))
        frame->selection().setSelectedRange(range, Affinity::Upstream, WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);

    if (!m_hasAnyActiveTouchPoints) {
        // Ensure that `Touch` doesn't linger around in `m_activeTextInteractionSources` after
        // the user has ended all active touches.
        removeTextInteractionSources(TextInteractionSource::Touch);
    }

    callback(selectionStart == initialSelectionStartPosition);
}

void WebPage::updateSelectionWithExtentPoint(const WebCore::IntPoint& point, bool isInteractingWithFocusedElement, RespectSelectionAnchor respectSelectionAnchor, CompletionHandler<void(bool)>&& callback)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return callback(false);

    auto position = visiblePositionInFocusedNodeForPoint(*frame, point, isInteractingWithFocusedElement);

    if (position.isNull())
        return callback(false);

    VisiblePosition selectionStart;
    VisiblePosition selectionEnd;
    
    if (respectSelectionAnchor == RespectSelectionAnchor::Yes) {
        if (m_selectionAnchor == SelectionAnchor::Start) {
            selectionStart = frame->selection().selection().visibleStart();
            selectionEnd = position;
            if (position <= selectionStart) {
                selectionStart = selectionStart.previous();
                selectionEnd = frame->selection().selection().visibleEnd();
                m_selectionAnchor = SelectionAnchor::End;
            }
        } else {
            selectionStart = position;
            selectionEnd = frame->selection().selection().visibleEnd();
            if (position >= selectionEnd) {
                selectionStart = frame->selection().selection().visibleStart();
                selectionEnd = selectionEnd.next();
                m_selectionAnchor = SelectionAnchor::Start;
            }
        }
    } else {
        auto currentStart = frame->selection().selection().visibleStart();
        auto currentEnd = frame->selection().selection().visibleEnd();
        if (position <= currentStart) {
            selectionStart = position;
            selectionEnd = currentEnd;
        } else if (position >= currentEnd) {
            selectionStart = currentStart;
            selectionEnd = position;
        }
    }
    
    if (auto range = makeSimpleRange(selectionStart, selectionEnd))
        frame->selection().setSelectedRange(range, Affinity::Upstream, WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);

    callback(m_selectionAnchor == SelectionAnchor::Start);
}

void WebPage::didReleaseAllTouchPoints()
{
    m_hasAnyActiveTouchPoints = false;
    removeTextInteractionSources(TextInteractionSource::Touch);
}

#if ENABLE(REVEAL)
RevealItem WebPage::revealItemForCurrentSelection()
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return { };

    auto selection = frame->selection().selection();
    if (!selection.isNone()) {
        std::optional<SimpleRange> fullCharacterRange;
        if (selection.isRange()) {
            auto selectionStart = selection.visibleStart();
            auto selectionEnd = selection.visibleEnd();

            // As context, we are going to use the surrounding paragraphs of text.
            fullCharacterRange = makeSimpleRange(startOfParagraph(selectionStart), endOfParagraph(selectionEnd));
            if (!fullCharacterRange)
                fullCharacterRange = makeSimpleRange(selectionStart, selectionEnd);
            
            if (fullCharacterRange) {
                auto selectionRange = NSMakeRange(characterCount(*makeSimpleRange(fullCharacterRange->start, selectionStart)), characterCount(*makeSimpleRange(selectionStart, selectionEnd)));
                String itemString = plainText(*fullCharacterRange);
                return { itemString, selectionRange };
            }
        }
    }
    return { };
}

void WebPage::requestRVItemInCurrentSelectedRange(CompletionHandler<void(const WebKit::RevealItem&)>&& completionHandler)
{
    completionHandler(RevealItem(revealItemForCurrentSelection()));
}

void WebPage::prepareSelectionForContextMenuWithLocationInView(IntPoint point, CompletionHandler<void(bool, const RevealItem&)>&& completionHandler)
{
    constexpr OptionSet hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return completionHandler(false, { });
    Ref frame = *localMainFrame;
    auto result = frame->eventHandler().hitTestResultAtPoint(point, hitType);
    RefPtr hitNode = result.innerNonSharedNode();
    if (!hitNode)
        return completionHandler(false, { });

    if (RefPtr view = frame->view()) {
        auto pointInContents = view->rootViewToContents(point);

        if (frame->selection().contains(pointInContents))
            return completionHandler(true, revealItemForCurrentSelection());
    }

    auto sendEditorStateAndCallCompletionHandler = [this, completionHandler = WTFMove(completionHandler)](RevealItem&& item) mutable {
        layoutIfNeeded();
        sendEditorStateUpdate();
        completionHandler(true, WTFMove(item));
    };

    if (is<HTMLImageElement>(*hitNode) && hitNode->hasEditableStyle()) {
        auto range = makeRangeSelectingNode(*hitNode);
        if (range && frame->selection().setSelectedRange(range, Affinity::Upstream, FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes))
            return sendEditorStateAndCallCompletionHandler({ });
    }

    frame->eventHandler().selectClosestContextualWordOrLinkFromHitTestResult(result, DontAppendTrailingWhitespace);
    sendEditorStateAndCallCompletionHandler(revealItemForCurrentSelection());
}
#endif

void WebPage::replaceSelectedText(const String& oldText, const String& newText)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    auto wordRange = frame->selection().isCaret() ? wordRangeFromPosition(frame->selection().selection().start()) : frame->selection().selection().toNormalizedRange();
    if (plainTextForContext(wordRange) != oldText)
        return;

    IgnoreSelectionChangeForScope ignoreSelectionChanges { *frame };
    frame->selection().setSelectedRange(wordRange, Affinity::Upstream, WebCore::FrameSelection::ShouldCloseTyping::Yes);
    frame->editor().insertText(newText, 0);
}

void WebPage::replaceDictatedText(const String& oldText, const String& newText)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    if (frame->selection().isNone())
        return;
    
    if (frame->selection().isRange()) {
        frame->editor().deleteSelectionWithSmartDelete(false);
        return;
    }
    VisiblePosition position = frame->selection().selection().start();
    for (auto i = numGraphemeClusters(oldText); i; --i)
        position = position.previous();
    if (position.isNull())
        position = startOfDocument(frame->document());
    auto range = makeSimpleRange(position, frame->selection().selection().start());

    if (plainTextForContext(range) != oldText)
        return;

    // We don't want to notify the client that the selection has changed until we are done inserting the new text.
    IgnoreSelectionChangeForScope ignoreSelectionChanges { *frame };
    frame->selection().setSelectedRange(range, Affinity::Upstream, WebCore::FrameSelection::ShouldCloseTyping::Yes);
    frame->editor().insertText(newText, 0);
}

void WebPage::willInsertFinalDictationResult()
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    if (frame->selection().isNone())
        return;

    m_ignoreSelectionChangeScopeForDictation = makeUnique<IgnoreSelectionChangeForScope>(*frame);
}

bool WebPage::shouldRemoveDictationAlternativesAfterEditing() const
{
    return !m_ignoreSelectionChangeScopeForDictation;
}

void WebPage::didInsertFinalDictationResult()
{
    m_ignoreSelectionChangeScopeForDictation = nullptr;
    scheduleFullEditorStateUpdate();
}

void WebPage::requestAutocorrectionData(const String& textForAutocorrection, CompletionHandler<void(WebAutocorrectionData)>&& reply)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return reply({ });

    if (!frame->selection().isCaret()) {
        reply({ });
        return;
    }

    auto range = wordRangeFromPosition(frame->selection().selection().visibleStart());
    if (!range) {
        reply({ });
        return;
    }

    auto textForRange = plainTextForContext(range);
    const unsigned maxSearchAttempts = 5;
    for (size_t i = 0;  i < maxSearchAttempts && textForRange != textForAutocorrection; ++i) {
        auto position = makeDeprecatedLegacyPosition(range->start).previous();
        if (position.isNull() || position == makeDeprecatedLegacyPosition(range->start))
            break;
        auto startRange = wordRangeFromPosition(position);
        if (!startRange)
            continue;
        range = { { startRange->start, range->end } };
        textForRange = plainTextForContext(range);
    }

    Vector<SelectionGeometry> selectionGeometries;
    if (textForRange == textForAutocorrection)
        selectionGeometries = RenderObject::collectSelectionGeometries(*range).geometries;

    auto rootViewSelectionRects = selectionGeometries.map([&](const auto& selectionGeometry) -> FloatRect {
        return frame->view()->contentsToRootView(selectionGeometry.rect());
    });

    bool multipleFonts = false;
    CTFontRef font = nil;
    if (auto coreFont = frame->editor().fontForSelection(multipleFonts))
        font = coreFont->getCTFont();

    reply({ WTFMove(rootViewSelectionRects) , (__bridge UIFont *)font });
}

void WebPage::applyAutocorrection(const String& correction, const String& originalText, bool isCandidate, CompletionHandler<void(const String&)>&& callback)
{
    callback(applyAutocorrectionInternal(correction, originalText, isCandidate) ? correction : String());
}

Seconds WebPage::eventThrottlingDelay() const
{
    auto behaviorOverride = m_page->eventThrottlingBehaviorOverride();
    if (behaviorOverride) {
        switch (behaviorOverride.value()) {
        case EventThrottlingBehavior::Responsive:
            return 0_s;
        case EventThrottlingBehavior::Unresponsive:
            return 1_s;
        }
    }

    if (m_isInStableState || m_estimatedLatency <= Seconds(1.0 / 60))
        return 0_s;

    return std::min(m_estimatedLatency * 2, 1_s);
}

void WebPage::syncApplyAutocorrection(const String& correction, const String& originalText, bool isCandidate, CompletionHandler<void(bool)>&& reply)
{
    reply(applyAutocorrectionInternal(correction, originalText, isCandidate));
}

bool WebPage::applyAutocorrectionInternal(const String& correction, const String& originalText, bool isCandidate)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return false;

    if (!frame->selection().isCaretOrRange())
        return false;

    if (correction == originalText)
        return false;

    std::optional<SimpleRange> range;
    String textForRange;
    auto originalTextWithFoldedQuoteMarks = foldQuoteMarks(originalText);

    if (frame->selection().isCaret()) {
        auto position = frame->selection().selection().visibleStart();
        range = wordRangeFromPosition(position);
        textForRange = plainTextForContext(range);
        
        // If 'originalText' is not the same as 'textForRange' we need to move 'range'
        // forward such that it matches the original selection as much as possible.
        if (foldQuoteMarks(textForRange) != originalTextWithFoldedQuoteMarks) {
            // Search for the original text near the selection caret.
            auto characterCount = numGraphemeClusters(originalText);
            if (!characterCount) {
                textForRange = emptyString();
                range = makeSimpleRange(position);
            } else if (auto searchRange = rangeExpandedAroundPositionByCharacters(position, characterCount)) {
                if (auto foundRange = findPlainText(*searchRange, originalTextWithFoldedQuoteMarks, { FindOption::DoNotSetSelection, FindOption::DoNotRevealSelection }); !foundRange.collapsed()) {
                    textForRange = plainTextForContext(foundRange);
                    range = foundRange;
                }
            }
        } else if (textForRange.isEmpty() && range && !range->collapsed()) {
            // If 'range' does not include any text but it is not collapsed, we need to set
            // 'range' to match the selection. Otherwise non-text nodes will be removed.
            range = makeSimpleRange(position);
            if (!range)
                return false;
        }
    } else {
        // Range selection.
        range = frame->selection().selection().toNormalizedRange();
        if (!range)
            return false;

        textForRange = plainTextForContext(range);
    }

    if (foldQuoteMarks(textForRange) != originalTextWithFoldedQuoteMarks)
        return false;
    
    // Correctly determine affinity, using logic currently only present in VisiblePosition
    auto affinity = Affinity::Downstream;
    if (range && range->collapsed())
        affinity = VisiblePosition(makeDeprecatedLegacyPosition(range->start), Affinity::Upstream).affinity();
    
    frame->selection().setSelectedRange(range, affinity, WebCore::FrameSelection::ShouldCloseTyping::Yes);
    if (correction.length()) {
        frame->editor().insertText(correction, 0, originalText.isEmpty() ? TextEventInputKeyboard : TextEventInputAutocompletion);
        if (isCandidate)
            adjustCandidateAutocorrectionInFrame(correction, *frame);
    } else if (originalText.length())
        frame->editor().deleteWithDirection(SelectionDirection::Backward, TextGranularity::CharacterGranularity, false, true);
    return true;
}

WebAutocorrectionContext WebPage::autocorrectionContext()
{
    if (!m_focusedElement)
        return { };

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return { };

    if (!frame->selection().selection().hasEditableStyle())
        return { };

    String contextBefore;
    String markedText;
    String selectedText;
    String contextAfter;
    EditingRange selectedRangeInMarkedText;

    VisiblePosition startPosition = frame->selection().selection().start();
    VisiblePosition endPosition = frame->selection().selection().end();
    const unsigned minContextWordCount = 10;
    const unsigned minContextLength = 40;
    const unsigned maxContextLength = 100;

    if (frame->selection().isRange())
        selectedText = plainTextForContext(frame->selection().selection().toNormalizedRange());

    if (auto compositionRange = frame->editor().compositionRange()) {
        auto markedTextBefore = plainTextForContext(makeSimpleRange(compositionRange->start, startPosition));
        auto markedTextAfter = plainTextForContext(makeSimpleRange(endPosition, compositionRange->end));
        markedText = makeString(markedTextBefore, selectedText, markedTextAfter);
        if (!markedText.isEmpty()) {
            selectedRangeInMarkedText.location = markedTextBefore.length();
            selectedRangeInMarkedText.length = selectedText.length();
        }
    } else {
        auto firstPositionInEditableContent = startOfEditableContent(startPosition);
        if (startPosition != firstPositionInEditableContent) {
            VisiblePosition contextStartPosition = startPosition;
            VisiblePosition previousPosition;
            unsigned totalContextLength = 0;
            for (unsigned i = 0; i < minContextWordCount; ++i) {
                if (contextBefore.length() >= minContextLength)
                    break;
                previousPosition = startOfWord(positionOfNextBoundaryOfGranularity(contextStartPosition, TextGranularity::WordGranularity, SelectionDirection::Backward));
                if (previousPosition.isNull())
                    break;
                String currentWord = plainTextForContext(makeSimpleRange(previousPosition, contextStartPosition));
                totalContextLength += currentWord.length();
                if (totalContextLength >= maxContextLength)
                    break;
                contextStartPosition = previousPosition;
            }
            VisiblePosition sentenceContextStartPosition = startOfSentence(startPosition);
            if (sentenceContextStartPosition.isNotNull() && sentenceContextStartPosition < contextStartPosition)
                contextStartPosition = sentenceContextStartPosition;
            if (contextStartPosition.isNotNull() && contextStartPosition < startPosition) {
                contextBefore = plainTextForContext(makeSimpleRange(contextStartPosition, startPosition));
                if (atBoundaryOfGranularity(contextStartPosition, TextGranularity::ParagraphGranularity, SelectionDirection::Backward) && firstPositionInEditableContent != contextStartPosition)
                    contextBefore = makeString("\n "_s, contextBefore);
            }
        }

        if (endPosition != endOfEditableContent(endPosition)) {
            VisiblePosition nextPosition = endOfSentence(endPosition);
            if (nextPosition.isNotNull() && nextPosition > endPosition)
                contextAfter = plainTextForContext(makeSimpleRange(endPosition, nextPosition));
        }
    }

    WebAutocorrectionContext correction;
    correction.contextBefore = WTFMove(contextBefore);
    correction.markedText = WTFMove(markedText);
    correction.selectedText = WTFMove(selectedText);
    correction.contextAfter = WTFMove(contextAfter);
    correction.selectedRangeInMarkedText = WTFMove(selectedRangeInMarkedText);
    return correction;
}

void WebPage::preemptivelySendAutocorrectionContext()
{
    send(Messages::WebPageProxy::HandleAutocorrectionContext(autocorrectionContext()));
}

void WebPage::handleAutocorrectionContextRequest()
{
    send(Messages::WebPageProxy::HandleAutocorrectionContext(autocorrectionContext()));
}

void WebPage::prepareToRunModalJavaScriptDialog()
{
    // When a modal dialog is presented while an editable element is focused, UIKit will attempt to request a
    // WebAutocorrectionContext, which triggers synchronous IPC back to the web process, resulting in deadlock.
    // To avoid this deadlock, we preemptively compute and send autocorrection context data to the UI process,
    // such that the UI process can immediately respond to UIKit without synchronous IPC to the web process.
    preemptivelySendAutocorrectionContext();
}

static HTMLAnchorElement* containingLinkAnchorElement(Element& element)
{
    // FIXME: There is code in the drag controller that supports any link, even if it's not an HTMLAnchorElement. Why is this different?
    for (auto& currentElement : lineageOfType<HTMLAnchorElement>(element)) {
        if (currentElement.isLink())
            return &currentElement;
    }
    return nullptr;
}

static inline bool isAssistableElement(Element& element)
{
    if (is<HTMLSelectElement>(element))
        return true;
    if (is<HTMLTextAreaElement>(element))
        return true;
    if (RefPtr inputElement = dynamicDowncast<HTMLInputElement>(element)) {
        // FIXME: This laundry list of types is not a good way to factor this. Need a suitable function on HTMLInputElement itself.
#if ENABLE(INPUT_TYPE_WEEK_PICKER)
        if (inputElement->isWeekField())
            return true;
#endif
        return inputElement->isTextField() || inputElement->isDateField() || inputElement->isDateTimeLocalField() || inputElement->isMonthField() || inputElement->isTimeField() || inputElement->isColorControl();
    }
    if (is<HTMLIFrameElement>(element))
        return false;
    return element.isContentEditable();
}

static inline bool isObscuredElement(Element& element)
{
    RefPtr mainFrameDocument = element.document().mainFrameDocument();
    if (!mainFrameDocument) {
        LOG_ONCE(SiteIsolation, "Unable to properly perform isObscuredElement() without access to the main frame document ");
        return false;
    }

    auto elementRectInMainFrame = element.boundingBoxInRootViewCoordinates();

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AllowChildFrameContent, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::IgnoreClipping };
    HitTestResult result(elementRectInMainFrame.center());

    mainFrameDocument->hitTest(hitType, result);
    result.setToNonUserAgentShadowAncestor();

    if (result.targetElement() == &element)
        return false;

    return true;
}
    
static void focusedElementPositionInformation(WebPage& page, Element& focusedElement, const InteractionInformationRequest& request, InteractionInformationAtPosition& info)
{
    RefPtr frame = page.corePage()->focusController().focusedOrMainFrame();
    if (!frame || !frame->editor().hasComposition())
        return;

    const uint32_t kHitAreaWidth = 66;
    const uint32_t kHitAreaHeight = 66;
    Ref view = *frame->view();
    IntPoint adjustedPoint(view->rootViewToContents(request.point));
    IntPoint constrainedPoint = constrainPoint(adjustedPoint, *frame, focusedElement);
    VisiblePosition position = frame->visiblePositionForPoint(constrainedPoint);

    auto compositionRange = frame->editor().compositionRange();
    if (!compositionRange)
        return;

    auto startPosition = makeDeprecatedLegacyPosition(compositionRange->start);
    auto endPosition = makeDeprecatedLegacyPosition(compositionRange->end);
    if (position < startPosition)
        position = startPosition;
    else if (position > endPosition)
        position = endPosition;
    IntRect caretRect = view->contentsToRootView(position.absoluteCaretBounds());
    float deltaX = std::abs(caretRect.x() + (caretRect.width() / 2) - request.point.x());
    float deltaYFromTheTop = std::abs(caretRect.y() - request.point.y());
    float deltaYFromTheBottom = std::abs(caretRect.y() + caretRect.height() - request.point.y());

    info.isNearMarkedText = !(deltaX > kHitAreaWidth || deltaYFromTheTop > kHitAreaHeight || deltaYFromTheBottom > kHitAreaHeight);
}

static void linkIndicatorPositionInformation(WebPage& page, Element& linkElement, const InteractionInformationRequest& request, InteractionInformationAtPosition& info)
{
    if (!request.includeLinkIndicator)
        return;

    auto linkRange = makeRangeSelectingNodeContents(linkElement);
    float deviceScaleFactor = page.corePage()->deviceScaleFactor();
    const float marginInPoints = request.linkIndicatorShouldHaveLegacyMargins ? 4 : 0;

    constexpr OptionSet<TextIndicatorOption> textIndicatorOptions {
        TextIndicatorOption::TightlyFitContent,
        TextIndicatorOption::RespectTextColor,
        TextIndicatorOption::PaintBackgrounds,
        TextIndicatorOption::UseBoundingRectAndPaintAllContentForComplexRanges,
        TextIndicatorOption::IncludeMarginIfRangeMatchesSelection,
        TextIndicatorOption::ComputeEstimatedBackgroundColor
    };
    auto textIndicator = TextIndicator::createWithRange(linkRange, textIndicatorOptions, TextIndicatorPresentationTransition::None, FloatSize(marginInPoints * deviceScaleFactor, marginInPoints * deviceScaleFactor));

    info.textIndicator = WTFMove(textIndicator);
}
    
#if ENABLE(DATA_DETECTION)

static void dataDetectorLinkPositionInformation(Element& element, InteractionInformationAtPosition& info)
{
    if (!DataDetection::isDataDetectorLink(element))
        return;
    
    info.isDataDetectorLink = true;
    info.dataDetectorBounds = info.bounds;
    const int dataDetectionExtendedContextLength = 350;
    info.dataDetectorIdentifier = DataDetection::dataDetectorIdentifier(element);
    if (auto* results = element.document().frame()->dataDetectionResultsIfExists())
        info.dataDetectorResults = results->documentLevelResults();

    if (!DataDetection::requiresExtendedContext(element))
        return;
    
    auto range = makeRangeSelectingNodeContents(element);
    info.textBefore = plainTextForDisplay(rangeExpandedByCharactersInDirectionAtWordBoundary(makeDeprecatedLegacyPosition(range.start),
        dataDetectionExtendedContextLength, SelectionDirection::Backward));
    info.textAfter = plainTextForDisplay(rangeExpandedByCharactersInDirectionAtWordBoundary(makeDeprecatedLegacyPosition(range.end),
        dataDetectionExtendedContextLength, SelectionDirection::Forward));
}

static void dataDetectorImageOverlayPositionInformation(const HTMLElement& overlayHost, const InteractionInformationRequest& request, InteractionInformationAtPosition& info)
{
    RefPtr frame = overlayHost.document().frame();
    if (!frame)
        return;

    auto elementAndBounds = DataDetection::findDataDetectionResultElementInImageOverlay(request.point, overlayHost);
    if (!elementAndBounds)
        return;

    auto [foundElement, elementBounds] = *elementAndBounds;
    auto identifierValue = parseInteger<uint64_t>(foundElement->attributeWithoutSynchronization(HTMLNames::x_apple_data_detectors_resultAttr));
    if (!identifierValue || !*identifierValue)
        return;

    auto identifier = ObjectIdentifier<ImageOverlayDataDetectionResultIdentifierType>(*identifierValue);

    auto* dataDetectionResults = frame->dataDetectionResultsIfExists();
    if (!dataDetectionResults)
        return;

    auto dataDetectionResult = retainPtr(dataDetectionResults->imageOverlayDataDetectionResult(identifier));
    if (!dataDetectionResult)
        return;

    info.dataDetectorBounds = WTFMove(elementBounds);
    info.dataDetectorResults = @[ dataDetectionResult.get() ];
}

#endif // ENABLE(DATA_DETECTION)

static std::optional<std::pair<RenderImage&, Image&>> imageRendererAndImage(Element& element)
{
    auto* renderImage = dynamicDowncast<RenderImage>(element.renderer());
    if (!renderImage)
        return std::nullopt;

    if (!renderImage->cachedImage() || renderImage->cachedImage()->errorOccurred())
        return std::nullopt;

    auto* image = renderImage->cachedImage()->imageForRenderer(renderImage);
    if (!image || image->width() <= 1 || image->height() <= 1)
        return std::nullopt;

    return { { *renderImage, *image } };
}

static void videoPositionInformation(WebPage& page, HTMLVideoElement& element, const InteractionInformationRequest& request, InteractionInformationAtPosition& info)
{
    info.elementContainsImageOverlay = ImageOverlay::hasOverlay(element);

    if (!element.paused())
        return;

    auto renderVideo = element.renderer();
    if (!renderVideo)
        return;

    info.isPausedVideo = true;

    if (request.includeImageData)
        info.image = createShareableBitmap(*renderVideo);

    info.hostImageOrVideoElementContext = page.contextForElement(element);
}

static RefPtr<HTMLVideoElement> hostVideoElementIgnoringImageOverlay(Node& node)
{
    if (ImageOverlay::isInsideOverlay(node))
        return { };

    if (RefPtr video = dynamicDowncast<HTMLVideoElement>(node))
        return video;

    return dynamicDowncast<HTMLVideoElement>(node.shadowHost());
}

static void imagePositionInformation(WebPage& page, Element& element, const InteractionInformationRequest& request, InteractionInformationAtPosition& info)
{
    auto rendererAndImage = imageRendererAndImage(element);
    if (!rendererAndImage)
        return;

    auto& [renderImage, image] = *rendererAndImage;
    info.isImage = true;
    info.imageURL = page.applyLinkDecorationFiltering(element.document().completeURL(renderImage.cachedImage()->url().string()), LinkDecorationFilteringTrigger::Unspecified);
    info.imageMIMEType = image.mimeType();
    info.isAnimatedImage = image.isAnimated();
    info.isAnimating = image.isAnimating();
    RefPtr htmlElement = dynamicDowncast<HTMLElement>(element);
    info.elementContainsImageOverlay = htmlElement && ImageOverlay::hasOverlay(*htmlElement);
#if ENABLE(SPATIAL_IMAGE_DETECTION)
    info.isSpatialImage = image.isSpatial();
#endif

    if (request.includeSnapshot || request.includeImageData)
        info.image = createShareableBitmap(renderImage, { screenSize() * page.corePage()->deviceScaleFactor(), AllowAnimatedImages::Yes, UseSnapshotForTransparentImages::Yes });

    info.hostImageOrVideoElementContext = page.contextForElement(element);
}

static void boundsPositionInformation(RenderObject& renderer, InteractionInformationAtPosition& info)
{
    if (CheckedPtr renderImage = dynamicDowncast<RenderImage>(renderer))
        info.bounds = renderImage->absoluteContentQuad().enclosingBoundingBox();
    else
        info.bounds = renderer.absoluteBoundingBoxRect();

    if (!renderer.document().frame()->isMainFrame()) {
        auto* view = renderer.document().frame()->view();
        info.bounds = view->contentsToRootView(info.bounds);
    }
}

static void elementPositionInformation(WebPage& page, Element& element, const InteractionInformationRequest& request, const Node* innerNonSharedNode, InteractionInformationAtPosition& info)
{
    Ref document = element.document();
    Element* linkElement = nullptr;
    if (element.renderer() && element.renderer()->isRenderImage())
        linkElement = containingLinkAnchorElement(element);
    else if (element.isLink())
        linkElement = &element;

    info.isElement = true;
    info.idAttribute = element.getIdAttribute();
    info.isImageOverlayText = ImageOverlay::isOverlayText(innerNonSharedNode);

    info.title = element.attributeWithoutSynchronization(HTMLNames::titleAttr).string();
    if (linkElement && info.title.isEmpty())
        info.title = element.innerText();
    if (element.renderer())
        info.touchCalloutEnabled = element.renderer()->style().touchCallout() == WebCore::Style::WebkitTouchCallout::Default;

    if (linkElement && !info.isImageOverlayText) {
        info.isLink = true;
        info.url = page.applyLinkDecorationFiltering(document->completeURL(linkElement->getAttribute(HTMLNames::hrefAttr)), LinkDecorationFilteringTrigger::Unspecified);

        linkIndicatorPositionInformation(page, *linkElement, request, info);
#if ENABLE(DATA_DETECTION)
        dataDetectorLinkPositionInformation(element, info);
#endif
    }

    auto* elementForScrollTesting = linkElement ? linkElement : &element;
    if (auto* renderer = elementForScrollTesting->renderer()) {
#if ENABLE(ASYNC_SCROLLING)
        if (auto* scrollingCoordinator = page.scrollingCoordinator())
            info.containerScrollingNodeID = scrollingCoordinator->scrollableContainerNodeID(*renderer);
#endif
    }

    info.needsPointerTouchCompatibilityQuirk = document->quirks().needsPointerTouchCompatibility(element);

    if (auto* renderer = element.renderer()) {
        bool shouldCollectImagePositionInformation = renderer->isRenderImage();
        if (shouldCollectImagePositionInformation && info.isImageOverlayText) {
            shouldCollectImagePositionInformation = false;
            if (request.includeImageData) {
                if (auto rendererAndImage = imageRendererAndImage(element)) {
                    auto& [renderImage, image] = *rendererAndImage;
                    info.imageURL = page.applyLinkDecorationFiltering(document->completeURL(renderImage.cachedImage()->url().string()), LinkDecorationFilteringTrigger::Unspecified);
                    info.imageMIMEType = image.mimeType();
                    info.image = createShareableBitmap(renderImage, { screenSize() * page.corePage()->deviceScaleFactor(), AllowAnimatedImages::Yes, UseSnapshotForTransparentImages::Yes });
                }
            }
        }
        if (shouldCollectImagePositionInformation) {
            if (auto video = hostVideoElementIgnoringImageOverlay(element))
                videoPositionInformation(page, *video, request, info);
            else
                imagePositionInformation(page, element, request, info);
        }
        boundsPositionInformation(*renderer, info);
    }

    info.elementContext = page.contextForElement(element);
}
    
static void selectionPositionInformation(WebPage& page, const InteractionInformationRequest& request, InteractionInformationAtPosition& info)
{
    auto* localMainFrame = dynamicDowncast<WebCore::LocalFrame>(page.corePage()->mainFrame());
    if (!localMainFrame)
        return;

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    HitTestResult result = localMainFrame->eventHandler().hitTestResultAtPoint(request.point, hitType);
    Node* hitNode = result.innerNode();

    // Hit test could return HTMLHtmlElement that has no renderer, if the body is smaller than the document.
    if (!hitNode || !hitNode->renderer())
        return;

    auto* renderer = hitNode->renderer();

    info.selectability = ([&] {
        if (renderer->style().usedUserSelect() == UserSelect::None)
            return InteractionInformationAtPosition::Selectability::UnselectableDueToUserSelectNoneOrQuirk;

        if (RefPtr element = dynamicDowncast<Element>(*hitNode)) {
            if (isAssistableElement(*element))
                return InteractionInformationAtPosition::Selectability::UnselectableDueToFocusableElement;

            if (rectIsTooBigForSelection(info.bounds, *result.innerNodeFrame())) {
                // We don't want to select blocks that are larger than 97% of the visible area of the document.
                // FIXME: Is this heuristic still needed, now that block selection has been removed?
                return InteractionInformationAtPosition::Selectability::UnselectableDueToLargeElementBounds;
            }

            if (hostVideoElementIgnoringImageOverlay(*hitNode))
                return InteractionInformationAtPosition::Selectability::UnselectableDueToMediaControls;
        }

        if (hitNode->protectedDocument()->quirks().shouldAvoidStartingSelectionOnMouseDownOverPointerCursor(*hitNode))
            return InteractionInformationAtPosition::Selectability::UnselectableDueToUserSelectNoneOrQuirk;

        return InteractionInformationAtPosition::Selectability::Selectable;
    })();
    info.isSelected = result.isSelected();

    if (info.isLink || info.isImage)
        return;

    boundsPositionInformation(*renderer, info);
    
    if (RefPtr element = dynamicDowncast<Element>(*hitNode))
        info.idAttribute = element->getIdAttribute();
    
    if (RefPtr attachment = dynamicDowncast<HTMLAttachmentElement>(*hitNode)) {
        info.isAttachment = true;
        info.title = attachment->attachmentTitle();
        linkIndicatorPositionInformation(page, *attachment, request, info);
        if (attachment->file())
            info.url = URL::fileURLWithFileSystemPath(attachment->file()->path());
    }

    for (RefPtr currentNode = hitNode; currentNode; currentNode = currentNode->parentOrShadowHostNode()) {
        auto* renderer = currentNode->renderer();
        if (!renderer)
            continue;

        auto& style = renderer->style();
        if (style.usedUserSelect() == UserSelect::None && style.userDrag() == UserDrag::Element) {
            info.prefersDraggingOverTextSelection = true;
            break;
        }
    }
#if PLATFORM(MACCATALYST)
    bool isInsideFixedPosition;
    VisiblePosition caretPosition(renderer->positionForPoint(request.point, HitTestSource::User, nullptr));
    info.caretRect = caretPosition.absoluteCaretBounds(&isInsideFixedPosition);
#endif

#if ENABLE(MODEL_PROCESS)
    if (is<HTMLModelElement>(*hitNode))
        info.prefersDraggingOverTextSelection = true;
#endif
}

static void textInteractionPositionInformation(WebPage& page, const HTMLInputElement& input, const InteractionInformationRequest& request, InteractionInformationAtPosition& info)
{
    if (!input.list())
        return;

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    auto* localMainFrame = dynamicDowncast<WebCore::LocalFrame>(page.corePage()->mainFrame());
    if (!localMainFrame)
        return;
    HitTestResult result = localMainFrame->eventHandler().hitTestResultAtPoint(request.point, hitType);
    if (result.innerNode() == input.dataListButtonElement())
        info.preventTextInteraction = true;
}

RefPtr<ShareableBitmap> WebPage::shareableBitmapSnapshotForNode(Element& element)
{
    // Ensure that the image contains at most 600K pixels, so that it is not too big.
    if (auto snapshot = snapshotNode(element, SnapshotOption::Shareable, 600 * 1024))
        return snapshot->bitmap();
    return nullptr;
}

static bool canForceCaretForPosition(const VisiblePosition& position)
{
    auto* node = position.deepEquivalent().anchorNode();
    if (!node)
        return false;

    auto* renderer = node->renderer();
    auto* style = renderer ? &renderer->style() : nullptr;
    auto cursorType = style ? style->cursor() : CursorType::Auto;

    if (cursorType == CursorType::Text)
        return true;

    if (cursorType != CursorType::Auto)
        return false;

    if (node->hasEditableStyle())
        return true;

    if (!renderer)
        return false;

    return renderer->isRenderText() && node->canStartSelection();
}

static CursorContext cursorContext(const HitTestResult& hitTestResult, const InteractionInformationRequest& request)
{
    CursorContext context;
    RefPtr frame = hitTestResult.innerNodeFrame();
    if (!frame)
        return context;

    // FIXME: Do we really need to set the cursor here if `includeCursorContext` is not set?
    context.cursor = frame->eventHandler().selectCursor(hitTestResult, false);

    if (!request.includeCursorContext)
        return context;

    RefPtr view = frame->view();
    if (!view)
        return context;

    auto node = hitTestResult.innerNode();
    if (!node)
        return context;

    auto* renderer = node->renderer();
    if (!renderer)
        return context;

    while (renderer && !is<RenderBlockFlow>(*renderer))
        renderer = renderer->parent();

    if (!renderer)
        return context;

    // FIXME: We should be able to retrieve this geometry information without
    // forcing the text to fall out of Simple Line Layout.
    auto& blockFlow = downcast<RenderBlockFlow>(*renderer);
    auto position = frame->visiblePositionForPoint(view->rootViewToContents(request.point));
    auto lineRect = position.absoluteSelectionBoundsForLine();
    bool isEditable = node->hasEditableStyle();

    if (isEditable)
        lineRect.setWidth(blockFlow.contentBoxWidth());

    context.isVerticalWritingMode = !renderer->isHorizontalWritingMode();
    context.lineCaretExtent = view->contentsToRootView(lineRect);

    auto cursorTypeIs = [](const auto& maybeCursor, auto type) {
        return maybeCursor.transform([type](const auto& cursor) {
            return cursor.type() == type;
        }).value_or(false);
    };

    bool lineContainsRequestPoint = context.lineCaretExtent.contains(request.point);
    // Force an I-beam cursor if the page didn't request a hand, and we're inside the bounds of the line.
    if (lineContainsRequestPoint && !cursorTypeIs(context.cursor, Cursor::Type::Hand) && canForceCaretForPosition(position))
        context.cursor = Cursor::fromType(Cursor::Type::IBeam);

    if (!lineContainsRequestPoint && cursorTypeIs(context.cursor, Cursor::Type::IBeam)) {
        auto approximateLineRectInContentCoordinates = renderer->absoluteBoundingBoxRect();
        approximateLineRectInContentCoordinates.setHeight(renderer->style().computedLineHeight());
        context.lineCaretExtent = view->contentsToRootView(approximateLineRectInContentCoordinates);
        if (!context.lineCaretExtent.contains(request.point) || !isEditable)
            context.lineCaretExtent.setY(request.point.y() - context.lineCaretExtent.height() / 2);
    }

    auto nodeShouldNotUseIBeam = ^(Node* node) {
        if (!node)
            return false;
        RenderObject *renderer = node->renderer();
        if (!renderer)
            return false;
        return is<RenderReplaced>(*renderer);
    };

    const auto& deepPosition = position.deepEquivalent();
    context.shouldNotUseIBeamInEditableContent = nodeShouldNotUseIBeam(node) || nodeShouldNotUseIBeam(deepPosition.computeNodeBeforePosition()) || nodeShouldNotUseIBeam(deepPosition.computeNodeAfterPosition());
    return context;
}

static void animationPositionInformation(WebPage& page, const InteractionInformationRequest& request, const HitTestResult& hitTestResult, InteractionInformationAtPosition& info)
{
#if ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
    info.canShowAnimationControls = page.corePage() && page.corePage()->settings().imageAnimationControlEnabled();
    if (!request.gatherAnimations)
        return;

    for (const auto& node : hitTestResult.listBasedTestResult()) {
        auto* element = dynamicDowncast<Element>(node.ptr());
        if (!element)
            continue;

        auto rendererAndImage = imageRendererAndImage(*element);
        if (!rendererAndImage || !rendererAndImage->second.isAnimated())
            continue;

        if (auto elementContext = page.contextForElement(*element))
            info.animationsAtPoint.append({ WTFMove(*elementContext), rendererAndImage->second.isAnimating() });
    }
#else
    UNUSED_PARAM(page);
    UNUSED_PARAM(request);
    UNUSED_PARAM(info);
#endif // ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
}

InteractionInformationAtPosition WebPage::positionInformation(const InteractionInformationRequest& request)
{
    InteractionInformationAtPosition info;
    info.request = request;

    FloatPoint adjustedPoint;
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return info;

    auto* nodeRespondingToClickEvents = localMainFrame->nodeRespondingToClickEvents(request.point, adjustedPoint);

    info.isContentEditable = nodeRespondingToClickEvents && nodeRespondingToClickEvents->isContentEditable();
    info.adjustedPointForNodeRespondingToClickEvents = adjustedPoint;

    if (request.includeHasDoubleClickHandler)
        info.nodeAtPositionHasDoubleClickHandler = localMainFrame->nodeRespondingToDoubleClickEvent(request.point, adjustedPoint);

    auto hitTestRequestTypes = OptionSet<HitTestRequest::Type> {
        HitTestRequest::Type::ReadOnly,
        HitTestRequest::Type::AllowFrameScrollbars,
        HitTestRequest::Type::AllowVisibleChildFrameContentOnly,
    };

#if ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
    if (request.gatherAnimations) {
        hitTestRequestTypes.add(HitTestRequest::Type::IncludeAllElementsUnderPoint);
        hitTestRequestTypes.add(HitTestRequest::Type::CollectMultipleElements);
    }
#endif // ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)

    auto& eventHandler = localMainFrame->eventHandler();
    auto hitTestResult = eventHandler.hitTestResultAtPoint(request.point, hitTestRequestTypes);

#if ENABLE(PDF_PLUGIN)
    RefPtr pluginView = hitTestResult.isOverWidget() ? pluginViewForFrame(hitTestResult.innerNodeFrame()) : nullptr;
#endif

    info.cursorContext = [&] {
        if (request.includeCursorContext) {
#if ENABLE(PDF_PLUGIN)
            if (pluginView)
                return pluginView->cursorContext(request.point);
#endif
        }
        return cursorContext(hitTestResult, request);
    }();

    if (m_focusedElement)
        focusedElementPositionInformation(*this, *m_focusedElement, request, info);

    RefPtr hitTestNode = hitTestResult.innerNonSharedNode();
    if (RefPtr element = dynamicDowncast<Element>(nodeRespondingToClickEvents)) {
        elementPositionInformation(*this, *element, request, hitTestNode.get(), info);

        if (info.isLink && !info.isImage && request.includeSnapshot)
            info.image = shareableBitmapSnapshotForNode(*element);
    }

#if ENABLE(DATA_DETECTION)
    auto hitTestedImageOverlayHost = ([&]() -> RefPtr<HTMLElement> {
        if (!hitTestNode || !info.isImageOverlayText)
            return nullptr;

        RefPtr htmlElement = dynamicDowncast<HTMLElement>(hitTestNode->shadowHost());
        if (!htmlElement || !ImageOverlay::hasOverlay(*htmlElement))
            return nullptr;

        return htmlElement;
    })();

    if (hitTestedImageOverlayHost)
        dataDetectorImageOverlayPositionInformation(*hitTestedImageOverlayHost, request, info);
#endif // ENABLE(DATA_DETECTION)

    if (!info.isImage && request.includeImageData && hitTestNode) {
        if (auto video = hostVideoElementIgnoringImageOverlay(*hitTestNode))
            videoPositionInformation(*this, *video, request, info);
        else if (RefPtr img = dynamicDowncast<HTMLImageElement>(hitTestNode))
            imagePositionInformation(*this, *img, request, info);
    }

    animationPositionInformation(*this, request, hitTestResult, info);
    selectionPositionInformation(*this, request, info);

    // Prevent the callout bar from showing when tapping on the datalist button.
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(nodeRespondingToClickEvents))
        textInteractionPositionInformation(*this, *input, request, info);

#if ENABLE(MODEL_PROCESS)
    if (RefPtr modelElement = dynamicDowncast<HTMLModelElement>(hitTestNode))
        info.isInteractiveModel = modelElement->model() && modelElement->supportsStageModeInteraction();
#endif

#if ENABLE(PDF_PLUGIN)
    if (pluginView) {
        if (auto&& [url, bounds, textIndicator] = pluginView->linkDataAtPoint(request.point); !url.isEmpty()) {
            info.isLink = true;
            info.url = WTFMove(url);
            info.bounds = enclosingIntRect(bounds);
            info.textIndicator = WTFMove(textIndicator);
        }
        info.isInPlugin = true;
    }
#endif

    return info;
}

void WebPage::requestPositionInformation(const InteractionInformationRequest& request)
{
    sendEditorStateUpdate();
    send(Messages::WebPageProxy::DidReceivePositionInformation(positionInformation(request)));
}

void WebPage::startInteractionWithElementContextOrPosition(std::optional<WebCore::ElementContext>&& elementContext, WebCore::IntPoint&& point)
{
    if (elementContext) {
        m_interactionNode = elementForContext(*elementContext);
        if (m_interactionNode)
            return;
    }

    FloatPoint adjustedPoint;
    if (RefPtr localMainFrame = m_page->localMainFrame())
        m_interactionNode = localMainFrame->nodeRespondingToInteraction(point, adjustedPoint);
}

void WebPage::stopInteraction()
{
    m_interactionNode = nullptr;
}

static void handleAnimationActions(Element& element, uint32_t action)
{
#if ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
    if (static_cast<SheetAction>(action) == SheetAction::PlayAnimation) {
        if (RefPtr imageElement = dynamicDowncast<HTMLImageElement>(element))
            imageElement->setAllowsAnimation(true);
    } else if (static_cast<SheetAction>(action) == SheetAction::PauseAnimation) {
        if (RefPtr imageElement = dynamicDowncast<HTMLImageElement>(element))
            imageElement->setAllowsAnimation(false);
    }
#endif // ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
}

void WebPage::performActionOnElement(uint32_t action, const String& authorizationToken, CompletionHandler<void()>&& completionHandler)
{
    CompletionHandlerCallingScope callCompletionHandler(WTFMove(completionHandler));

    RefPtr element = dynamicDowncast<HTMLElement>(m_interactionNode);
    if (!element || !element->renderer())
        return;

    if (static_cast<SheetAction>(action) == SheetAction::Copy) {
        if (is<RenderImage>(*element->renderer())) {
            URL urlToCopy;
            String titleToCopy;
            if (RefPtr linkElement = containingLinkAnchorElement(*element)) {
                if (auto url = linkElement->href(); !url.isEmpty() && !url.protocolIsJavaScript()) {
                    urlToCopy = url;
                    titleToCopy = linkElement->attributeWithoutSynchronization(HTMLNames::titleAttr);
                    if (!titleToCopy.length())
                        titleToCopy = linkElement->textContent();
                    titleToCopy = titleToCopy.trim(isASCIIWhitespace);
                }
            }
            m_interactionNode->document().editor().writeImageToPasteboard(*Pasteboard::createForCopyAndPaste(PagePasteboardContext::create(element->document().pageID())), *element, urlToCopy, titleToCopy);
        } else if (element->isLink())
            m_interactionNode->document().editor().copyURL(element->document().completeURL(element->attributeWithoutSynchronization(HTMLNames::hrefAttr)), element->textContent());
#if ENABLE(ATTACHMENT_ELEMENT)
        else if (auto attachmentInfo = element->document().editor().promisedAttachmentInfo(*element))
            send(Messages::WebPageProxy::WritePromisedAttachmentToPasteboard(WTFMove(attachmentInfo), authorizationToken));
#endif
    } else if (static_cast<SheetAction>(action) == SheetAction::SaveImage) {
        CheckedPtr renderImage = dynamicDowncast<RenderImage>(*element->renderer());
        if (!renderImage)
            return;
        auto* cachedImage = renderImage->cachedImage();
        if (!cachedImage)
            return;
        RefPtr<FragmentedSharedBuffer> buffer = cachedImage->resourceBuffer();
        if (!buffer)
            return;
        std::optional<SharedMemory::Handle> handle;
        {
            auto sharedMemoryBuffer = SharedMemory::copyBuffer(*buffer);
            if (!sharedMemoryBuffer)
                return;
            handle = sharedMemoryBuffer->createHandle(SharedMemory::Protection::ReadOnly);
        }
        if (!handle)
            return;
        send(Messages::WebPageProxy::SaveImageToLibrary(WTFMove(*handle), authorizationToken));
    }
#if ENABLE(SPATIAL_IMAGE_DETECTION)
    else if (static_cast<SheetAction>(action) == SheetAction::ViewSpatial)
        element->webkitRequestFullscreen();
#endif

    handleAnimationActions(*element, action);
}

void WebPage::performActionOnElements(uint32_t action, const Vector<WebCore::ElementContext>& elements)
{
    for (const auto& elementContext : elements) {
        if (RefPtr element = elementForContext(elementContext))
            handleAnimationActions(*element, action);
    }
}

static inline RefPtr<Element> nextAssistableElement(Node* startNode, Page& page, bool isForward)
{
    RefPtr nextElement = dynamicDowncast<Element>(startNode);
    if (!nextElement)
        return nullptr;

    CheckedRef focusController { page.focusController() };
    do {
        auto result = isForward ? focusController->nextFocusableElement(*nextElement) : focusController->previousFocusableElement(*nextElement);
        if (!result.element && result.continuedSearchInRemoteFrame == ContinuedSearchInRemoteFrame::Yes)
            RELEASE_LOG(SiteIsolation, "Crossing site isolation process barrier searching for `nextAssistableElement` is not yet supported");

        nextElement = result.element;
    } while (nextElement && (!isAssistableElement(*nextElement) || isObscuredElement(*nextElement)));

    return nextElement;
}

void WebPage::focusNextFocusedElement(bool isForward, CompletionHandler<void()>&& completionHandler)
{
    auto nextElement = nextAssistableElement(m_focusedElement.get(), *m_page, isForward);
    m_userIsInteracting = true;
    if (nextElement)
        nextElement->focus();
    m_userIsInteracting = false;
    completionHandler();
}

std::optional<FocusedElementInformation> WebPage::focusedElementInformation()
{
    RefPtr focusedOrMainFrame = m_page->focusController().focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return std::nullopt;
    RefPtr<Document> document = focusedOrMainFrame->document();
    if (!document || !document->view())
        return std::nullopt;

    auto focusedElement = m_focusedElement.copyRef();
    layoutIfNeeded();

    // Layout may have detached the document or caused a change of focus.
    if (!document->view() || focusedElement != m_focusedElement)
        return std::nullopt;

    scheduleFullEditorStateUpdate();

    FocusedElementInformation information;

    if (RefPtr webFrame = WebProcess::singleton().webFrame(focusedOrMainFrame->frameID()))
        information.frame = webFrame->info();

    information.lastInteractionLocation = m_lastInteractionLocation;
    if (auto elementContext = contextForElement(*focusedElement))
        information.elementContext = WTFMove(*elementContext);

    if (auto* renderer = focusedElement->renderer()) {
        information.interactionRect = rootViewInteractionBounds(*focusedElement);
        information.nodeFontSize = renderer->style().fontDescription().computedSize();

        bool inFixed = false;
        renderer->localToContainerPoint(FloatPoint(), nullptr, UseTransforms, &inFixed);
        information.insideFixedPosition = inFixed;
        information.isRTL = renderer->writingMode().isBidiRTL();

#if ENABLE(ASYNC_SCROLLING)
        if (auto* scrollingCoordinator = this->scrollingCoordinator())
            information.containerScrollingNodeID = scrollingCoordinator->scrollableContainerNodeID(*renderer);
#endif
    } else
        information.interactionRect = { };

    RefPtr htmlElement = dynamicDowncast<HTMLElement>(focusedElement);
    if (htmlElement)
        information.isSpellCheckingEnabled = htmlElement->spellcheck();

    information.isWritingSuggestionsEnabled = focusedElement->isWritingSuggestionsEnabled();

    if (RefPtr formControlElement = dynamicDowncast<HTMLFormControlElement>(focusedElement))
        information.isFocusingWithValidationMessage = formControlElement->isFocusingWithValidationMessage();

    information.minimumScaleFactor = minimumPageScaleFactor();
    information.maximumScaleFactor = maximumPageScaleFactor();
    information.maximumScaleFactorIgnoringAlwaysScalable = maximumPageScaleFactorIgnoringAlwaysScalable();
    information.allowsUserScaling = m_viewportConfiguration.allowsUserScaling();
    information.allowsUserScalingIgnoringAlwaysScalable = m_viewportConfiguration.allowsUserScalingIgnoringAlwaysScalable();
    if (auto nextElement = nextAssistableElement(focusedElement.get(), *m_page, true)) {
        information.nextNodeRect = rootViewBounds(*nextElement);
        information.hasNextNode = true;
    }
    if (auto previousElement = nextAssistableElement(focusedElement.get(), *m_page, false)) {
        information.previousNodeRect = rootViewBounds(*previousElement);
        information.hasPreviousNode = true;
    }
    information.identifier = m_internals->lastFocusedElementInformationIdentifier.increment();

    if (htmlElement) {
        if (auto labels = htmlElement->labels()) {
            Vector<Ref<Element>> associatedLabels;
            for (unsigned index = 0; index < labels->length(); ++index) {
                if (RefPtr label = dynamicDowncast<Element>(labels->item(index)); label && label->renderer())
                    associatedLabels.append(*label);
            }
            for (auto& labelElement : associatedLabels) {
                auto text = labelElement->innerText();
                if (!text.isEmpty()) {
                    information.label = WTFMove(text);
                    break;
                }
            }
        }
    }

    information.title = focusedElement->title();
    information.ariaLabel = focusedElement->attributeWithoutSynchronization(HTMLNames::aria_labelAttr);

    if (RefPtr element = dynamicDowncast<HTMLSelectElement>(*focusedElement)) {
#if USE(UICONTEXTMENU)
        static bool selectPickerUsesMenu = linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::HasUIContextMenuInteraction);
#else
        bool selectPickerUsesMenu = false;
#endif

        information.elementType = InputType::Select;

        RefPtr<ContainerNode> parentGroup;
        int parentGroupID = 0;
        for (auto& item : element->listItems()) {
            if (auto* optionElement = dynamicDowncast<HTMLOptionElement>(item.get())) {
                if (parentGroup && optionElement->parentNode() != parentGroup) {
                    parentGroupID++;
                    parentGroup = nullptr;
                    information.selectOptions.append(OptionItem(emptyString(), true, false, false, parentGroupID));
                }

                information.selectOptions.append(OptionItem(optionElement->label(), false, optionElement->selected(), optionElement->hasAttributeWithoutSynchronization(WebCore::HTMLNames::disabledAttr), parentGroupID));
            } else if (auto* optGroupElement = dynamicDowncast<HTMLOptGroupElement>(item.get())) {
                if (selectPickerUsesMenu)
                    parentGroup = optGroupElement;

                parentGroupID++;
                information.selectOptions.append(OptionItem(optGroupElement->groupLabelText(), true, false, optGroupElement->hasAttributeWithoutSynchronization(WebCore::HTMLNames::disabledAttr), parentGroupID));
            } else if (selectPickerUsesMenu && is<HTMLHRElement>(item.get())) {
                parentGroupID++;
                parentGroup = nullptr;
                information.selectOptions.append(OptionItem(emptyString(), true, false, false, parentGroupID));
            }
        }
        information.selectedIndex = element->selectedIndex();
        information.isMultiSelect = element->multiple();
    } else if (RefPtr element = dynamicDowncast<HTMLTextAreaElement>(*focusedElement)) {
        information.autocapitalizeType = element->autocapitalizeType();
        information.isAutocorrect = element->shouldAutocorrect();
        information.elementType = InputType::TextArea;
        information.isReadOnly = element->isReadOnly();
        information.value = element->value();
        information.hasPlainText = !information.value.isEmpty();
        information.autofillFieldName = WebCore::toAutofillFieldName(element->autofillData().fieldName);
        information.nonAutofillCredentialType = element->autofillData().nonAutofillCredentialType;
        information.placeholder = element->attributeWithoutSynchronization(HTMLNames::placeholderAttr);
        information.inputMode = element->canonicalInputMode();
        information.enterKeyHint = element->canonicalEnterKeyHint();
    } else if (RefPtr element = dynamicDowncast<HTMLInputElement>(*focusedElement)) {
        auto* form = element->form();
        if (form)
            information.formAction = form->getURLAttribute(WebCore::HTMLNames::actionAttr).string();
        if (auto autofillElements = WebCore::AutofillElements::computeAutofillElements(*element)) {
            information.acceptsAutofilledLoginCredentials = true;
            information.isAutofillableUsernameField = autofillElements->username() == focusedElement;
        }
        information.representingPageURL = element->document().urlForBindings();
        information.autocapitalizeType = element->autocapitalizeType();
        information.isAutocorrect = element->shouldAutocorrect();
        information.placeholder = element->attributeWithoutSynchronization(HTMLNames::placeholderAttr);
        information.hasEverBeenPasswordField = element->hasEverBeenPasswordField();
        if (element->isPasswordField())
            information.elementType = InputType::Password;
        else if (element->isSearchField())
            information.elementType = InputType::Search;
        else if (element->isEmailField())
            information.elementType = InputType::Email;
        else if (element->isTelephoneField())
            information.elementType = InputType::Phone;
        else if (element->isNumberField())
            information.elementType = element->getAttribute(HTMLNames::patternAttr) == "\\d*"_s || element->getAttribute(HTMLNames::patternAttr) == "[0-9]*"_s ? InputType::NumberPad : InputType::Number;
        else if (element->isDateTimeLocalField())
            information.elementType = InputType::DateTimeLocal;
        else if (element->isDateField())
            information.elementType = InputType::Date;
        else if (element->isTimeField())
            information.elementType = InputType::Time;
        else if (element->isWeekField())
            information.elementType = InputType::Week;
        else if (element->isMonthField())
            information.elementType = InputType::Month;
        else if (element->isURLField())
            information.elementType = InputType::URL;
        else if (element->isText()) {
            const AtomString& pattern = element->attributeWithoutSynchronization(HTMLNames::patternAttr);
            if (pattern == "\\d*"_s || pattern == "[0-9]*"_s)
                information.elementType = InputType::NumberPad;
            else {
                information.elementType = InputType::Text;
                if (!information.formAction.isEmpty()
                    && (element->getNameAttribute().contains("search"_s) || element->getIdAttribute().contains("search"_s) || element->attributeWithoutSynchronization(HTMLNames::titleAttr).contains("search"_s)))
                    information.elementType = InputType::Search;
            }
        }
        else if (element->isColorControl()) {
            information.elementType = InputType::Color;
            information.colorValue = element->valueAsColor();
            information.supportsAlpha = element->alpha() ? WebKit::ColorControlSupportsAlpha::Yes : WebKit::ColorControlSupportsAlpha::No;
            information.suggestedColors = element->suggestedColors();
        }

        information.isFocusingWithDataListDropdown = element->isFocusingWithDataListDropdown();
        information.hasSuggestions = !!element->list();
        information.inputMode = element->canonicalInputMode();
        information.enterKeyHint = element->canonicalEnterKeyHint();
        information.isReadOnly = element->isReadOnly();
        information.value = element->value();
        information.hasPlainText = !information.value.isEmpty();
        information.valueAsNumber = element->valueAsNumber();
        information.autofillFieldName = WebCore::toAutofillFieldName(element->autofillData().fieldName);
        information.nonAutofillCredentialType = element->autofillData().nonAutofillCredentialType;
    } else if (focusedElement->hasEditableStyle()) {
        information.elementType = InputType::ContentEditable;
        if (RefPtr focusedHTMLElement = dynamicDowncast<HTMLElement>(*focusedElement)) {
            information.isAutocorrect = focusedHTMLElement->shouldAutocorrect();
            information.autocapitalizeType = focusedHTMLElement->autocapitalizeType();
            information.inputMode = focusedHTMLElement->canonicalInputMode();
            information.enterKeyHint = focusedHTMLElement->canonicalEnterKeyHint();
            information.shouldSynthesizeKeyEventsForEditing = true;
        } else {
            information.isAutocorrect = true;
            information.autocapitalizeType = WebCore::AutocapitalizeType::Default;
        }
        information.isReadOnly = false;
        information.hasPlainText = hasAnyPlainText(makeRangeSelectingNodeContents(*focusedElement));
    }

    if (focusedElement->document().quirks().shouldSuppressAutocorrectionAndAutocapitalizationInHiddenEditableAreas() && isTransparentOrFullyClipped(*focusedElement)) {
        information.autocapitalizeType = WebCore::AutocapitalizeType::None;
        information.isAutocorrect = false;
    }

    auto& quirks = focusedElement->document().quirks();
    information.shouldAvoidResizingWhenInputViewBoundsChange = quirks.shouldAvoidResizingWhenInputViewBoundsChange();
    information.shouldAvoidScrollingWhenFocusedContentIsVisible = quirks.shouldAvoidScrollingWhenFocusedContentIsVisible();
    information.shouldUseLegacySelectPopoverDismissalBehaviorInDataActivation = quirks.shouldUseLegacySelectPopoverDismissalBehaviorInDataActivation();
    information.shouldHideSoftTopScrollEdgeEffect = quirks.shouldHideSoftTopScrollEdgeEffectDuringFocus(*focusedElement);

    return information;
}

void WebPage::autofillLoginCredentials(const String& username, const String& password)
{
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(m_focusedElement)) {
        if (auto autofillElements = AutofillElements::computeAutofillElements(*input))
            autofillElements->autofill(username, password);
    }
}

void WebPage::setViewportConfigurationViewLayoutSize(const FloatSize& size, double layoutSizeScaleFactorFromClient, double minimumEffectiveDeviceWidth)
{
    LOG_WITH_STREAM(VisibleRects, stream << "WebPage " << m_identifier << " setViewportConfigurationViewLayoutSize " << size << " layoutSizeScaleFactorFromClient " << layoutSizeScaleFactorFromClient << " minimumEffectiveDeviceWidth " << minimumEffectiveDeviceWidth);

    if (!m_viewportConfiguration.isKnownToLayOutWiderThanViewport())
        m_viewportConfiguration.setMinimumEffectiveDeviceWidthForShrinkToFit(0);

    if (size.isZero() && mainFramePlugInRejectsZeroViewLayoutSizeUpdates())
        return;

    bool mainFramePluginOverridesViewScale = mainFramePlugInDefersScalingToViewport();

    m_baseViewportLayoutSizeScaleFactor = [&] {
        if (!m_page->settings().automaticallyAdjustsViewScaleUsingMinimumEffectiveDeviceWidth())
            return 1.0;

        if (!minimumEffectiveDeviceWidth)
            return 1.0;

        if (minimumEffectiveDeviceWidth >= size.width())
            return 1.0;

        if (mainFramePluginOverridesViewScale)
            return 1.0;

        return size.width() / minimumEffectiveDeviceWidth;
    }();

    double layoutSizeScaleFactor = mainFramePluginOverridesViewScale ? 1.0 : layoutSizeScaleFactorFromClient * m_baseViewportLayoutSizeScaleFactor;

    auto previousLayoutSizeScaleFactor = m_viewportConfiguration.layoutSizeScaleFactor();
    if (!m_viewportConfiguration.setViewLayoutSize(size, layoutSizeScaleFactor, minimumEffectiveDeviceWidth))
        return;

    auto zoomToInitialScale = ZoomToInitialScale::No;
    auto newInitialScale = m_viewportConfiguration.initialScale();
    auto currentPageScaleFactor = pageScaleFactor();
    if (layoutSizeScaleFactor > previousLayoutSizeScaleFactor && newInitialScale > currentPageScaleFactor)
        zoomToInitialScale = ZoomToInitialScale::Yes;
    else if (layoutSizeScaleFactor < previousLayoutSizeScaleFactor && newInitialScale < currentPageScaleFactor)
        zoomToInitialScale = ZoomToInitialScale::Yes;

    viewportConfigurationChanged(zoomToInitialScale);
}

void WebPage::setDeviceOrientation(IntDegrees deviceOrientation)
{
    if (deviceOrientation == m_deviceOrientation)
        return;
    m_deviceOrientation = deviceOrientation;
#if ENABLE(ORIENTATION_EVENTS)
    if (RefPtr localMainFrame = m_page->localMainFrame())
        localMainFrame->orientationChanged();
#endif
}

void WebPage::setOverrideViewportArguments(const std::optional<WebCore::ViewportArguments>& arguments)
{
    m_page->setOverrideViewportArguments(arguments);
}

void WebPage::dynamicViewportSizeUpdate(const DynamicViewportSizeUpdate& target)
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;

    SetForScope dynamicSizeUpdateGuard(m_inDynamicSizeUpdate, true);
    // FIXME: this does not handle the cases where the content would change the content size or scroll position from JavaScript.
    // To handle those cases, we would need to redo this computation on every change until the next visible content rect update.
    LOG_WITH_STREAM(VisibleRects, stream << "\nWebPage::dynamicViewportSizeUpdate - viewLayoutSize " << target.viewLayoutSize << " targetUnobscuredRect " << target.unobscuredRect << " targetExposedContentRect " << target.exposedContentRect << " targetScale " << target.scale);

    auto& frameView = *localMainFrame->view();
    IntSize oldContentSize = frameView.contentsSize();
    float oldPageScaleFactor = m_page->pageScaleFactor();
    auto oldUnobscuredContentRect = frameView.unobscuredContentRect();
    bool wasAtInitialScale = scalesAreEssentiallyEqual(oldPageScaleFactor, m_viewportConfiguration.initialScale());

    m_internals->dynamicSizeUpdateHistory.add(std::make_pair(oldContentSize, oldPageScaleFactor), frameView.scrollPosition());

    RefPtr<Node> oldNodeAtCenter;
    double visibleHorizontalFraction = 1;
    float relativeHorizontalPositionInNodeAtCenter = 0;
    float relativeVerticalPositionInNodeAtCenter = 0;
    if (!shouldEnableViewportBehaviorsForResizableWindows()) {
        visibleHorizontalFraction = frameView.unobscuredContentSize().width() / oldContentSize.width();
        IntPoint unobscuredContentRectCenter = frameView.unobscuredContentRect().center();

        HitTestResult hitTestResult = HitTestResult(unobscuredContentRectCenter);

        if (auto* document = frameView.frame().document())
            document->hitTest(HitTestRequest(), hitTestResult);

        if (Node* node = hitTestResult.innerNode()) {
            if (RenderObject* renderer = node->renderer()) {
                auto& containingView = *node->document().frame()->view();
                FloatRect boundingBox = containingView.contentsToRootView(renderer->absoluteBoundingBoxRect(true));
                relativeHorizontalPositionInNodeAtCenter = (unobscuredContentRectCenter.x() - boundingBox.x()) / boundingBox.width();
                relativeVerticalPositionInNodeAtCenter = (unobscuredContentRectCenter.y() - boundingBox.y()) / boundingBox.height();
                oldNodeAtCenter = node;
            }
        }
    }

    LOG_WITH_STREAM(VisibleRects, stream << "WebPage::dynamicViewportSizeUpdate setting view layout size to " << target.viewLayoutSize);
    bool viewportChanged = m_viewportConfiguration.setIsKnownToLayOutWiderThanViewport(false);
    viewportChanged |= m_viewportConfiguration.setViewLayoutSize(target.viewLayoutSize, std::nullopt, target.minimumEffectiveDeviceWidth);
    if (viewportChanged)
        viewportConfigurationChanged();

    IntSize newLayoutSize = m_viewportConfiguration.layoutSize();

    if (setFixedLayoutSize(newLayoutSize))
        resetTextAutosizing();

    setDefaultUnobscuredSize(target.maximumUnobscuredSize);
    setMinimumUnobscuredSize(target.minimumUnobscuredSize);
    setMaximumUnobscuredSize(target.maximumUnobscuredSize);
    m_page->setUnobscuredSafeAreaInsets(target.unobscuredSafeAreaInsets);

    frameView.updateLayoutAndStyleIfNeededRecursive();

    IntSize newContentSize = frameView.contentsSize();

    bool scaleToFitContent = (!shouldEnableViewportBehaviorsForResizableWindows() || !wasAtInitialScale) && m_userHasChangedPageScaleFactor;
    double scale = scaleAfterViewportWidthChange(target.scale, scaleToFitContent, m_viewportConfiguration, target.unobscuredRectInScrollViewCoordinates.width(), newContentSize, oldContentSize, visibleHorizontalFraction);
    FloatRect newUnobscuredContentRect = target.unobscuredRect;
    FloatRect newExposedContentRect = target.exposedContentRect;

    bool scaleChanged = !scalesAreEssentiallyEqual(scale, target.scale);
    if (scaleChanged) {
        // The target scale the UI is using cannot be reached by the content. We need to compute new targets based
        // on the viewport constraint and report everything back to the UIProcess.

        // 1) Compute a new unobscured rect centered around the original one.
        double scaleDifference = target.scale / scale;
        double newUnobscuredRectWidth = target.unobscuredRect.width() * scaleDifference;
        double newUnobscuredRectHeight = target.unobscuredRect.height() * scaleDifference;
        double newUnobscuredRectX;
        double newUnobscuredRectY;
        if (shouldEnableViewportBehaviorsForResizableWindows()) {
            newUnobscuredRectX = oldUnobscuredContentRect.x();
            newUnobscuredRectY = oldUnobscuredContentRect.y();
        } else {
            newUnobscuredRectX = target.unobscuredRect.x() - (newUnobscuredRectWidth - target.unobscuredRect.width()) / 2;
            newUnobscuredRectY = target.unobscuredRect.y() - (newUnobscuredRectHeight - target.unobscuredRect.height()) / 2;
        }
        newUnobscuredContentRect = FloatRect(newUnobscuredRectX, newUnobscuredRectY, newUnobscuredRectWidth, newUnobscuredRectHeight);

        // 2) Extend our new unobscuredRect by the obscured margins to get a new exposed rect.
        double obscuredTopMargin = (target.unobscuredRect.y() - target.exposedContentRect.y()) * scaleDifference;
        double obscuredLeftMargin = (target.unobscuredRect.x() - target.exposedContentRect.x()) * scaleDifference;
        double obscuredBottomMargin = (target.exposedContentRect.maxY() - target.unobscuredRect.maxY()) * scaleDifference;
        double obscuredRightMargin = (target.exposedContentRect.maxX() - target.unobscuredRect.maxX()) * scaleDifference;
        newExposedContentRect = FloatRect(newUnobscuredRectX - obscuredLeftMargin,
                                          newUnobscuredRectY - obscuredTopMargin,
                                          newUnobscuredRectWidth + obscuredLeftMargin + obscuredRightMargin,
                                          newUnobscuredRectHeight + obscuredTopMargin + obscuredBottomMargin);
    }

    if (oldContentSize != newContentSize || scaleChanged) {
        // Snap the new unobscured rect back into the content rect.
        newUnobscuredContentRect.setWidth(std::min(static_cast<float>(newContentSize.width()), newUnobscuredContentRect.width()));
        newUnobscuredContentRect.setHeight(std::min(static_cast<float>(newContentSize.height()), newUnobscuredContentRect.height()));

        bool positionWasRestoredFromSizeUpdateHistory = false;
        const auto& previousPosition = m_internals->dynamicSizeUpdateHistory.find(std::pair<IntSize, float>(newContentSize, scale));
        if (previousPosition != m_internals->dynamicSizeUpdateHistory.end()) {
            IntPoint restoredPosition = previousPosition->value;
            FloatPoint deltaPosition(restoredPosition.x() - newUnobscuredContentRect.x(), restoredPosition.y() - newUnobscuredContentRect.y());
            newUnobscuredContentRect.moveBy(deltaPosition);
            newExposedContentRect.moveBy(deltaPosition);
            positionWasRestoredFromSizeUpdateHistory = true;
        } else if (oldContentSize != newContentSize) {
            FloatPoint newRelativeContentCenter;

            if (RenderObject* renderer = oldNodeAtCenter ? oldNodeAtCenter->renderer() : nullptr) {
                auto& containingView = *oldNodeAtCenter->document().frame()->view();
                FloatRect newBoundingBox = containingView.contentsToRootView(renderer->absoluteBoundingBoxRect(true));
                newRelativeContentCenter = FloatPoint(newBoundingBox.x() + relativeHorizontalPositionInNodeAtCenter * newBoundingBox.width(), newBoundingBox.y() + relativeVerticalPositionInNodeAtCenter * newBoundingBox.height());
            } else
                newRelativeContentCenter = relativeCenterAfterContentSizeChange(target.unobscuredRect, oldContentSize, newContentSize);

            FloatPoint newUnobscuredContentRectCenter = newUnobscuredContentRect.center();
            FloatPoint positionDelta(newRelativeContentCenter.x() - newUnobscuredContentRectCenter.x(), newRelativeContentCenter.y() - newUnobscuredContentRectCenter.y());
            newUnobscuredContentRect.moveBy(positionDelta);
            newExposedContentRect.moveBy(positionDelta);
        }

        // Make the top/bottom edges "sticky" within 1 pixel.
        if (!positionWasRestoredFromSizeUpdateHistory) {
            if (target.unobscuredRect.maxY() > oldContentSize.height() - 1) {
                float bottomVerticalPosition = newContentSize.height() - newUnobscuredContentRect.height();
                newUnobscuredContentRect.setY(bottomVerticalPosition);
                newExposedContentRect.setY(bottomVerticalPosition);
            }
            if (target.unobscuredRect.y() < 1) {
                newUnobscuredContentRect.setY(0);
                newExposedContentRect.setY(0);
            }

            bool likelyResponsiveDesignViewport = newLayoutSize.width() == target.viewLayoutSize.width() && scalesAreEssentiallyEqual(scale, 1);
            bool contentBleedsOutsideLayoutWidth = newContentSize.width() > newLayoutSize.width();
            bool originalScrollPositionWasOnTheLeftEdge = target.unobscuredRect.x() <= 0;
            if (likelyResponsiveDesignViewport && contentBleedsOutsideLayoutWidth && originalScrollPositionWasOnTheLeftEdge) {
                // This is a special heuristics for "responsive" design with odd layout. It is quite common for responsive design
                // to have content "bleeding" outside of the minimal layout width, usually from an image or table larger than expected.
                // In those cases, the design usually does not adapt to the new width and remain at the newLayoutSize except for the
                // large boxes.
                // It is worth revisiting this special case as web developers get better with responsive design.
                newExposedContentRect.setX(0);
                newUnobscuredContentRect.setX(0);
            }
        }

        float horizontalAdjustment = 0;
        if (newUnobscuredContentRect.maxX() > newContentSize.width())
            horizontalAdjustment -= newUnobscuredContentRect.maxX() - newContentSize.width();
        float verticalAdjustment = 0;
        if (newUnobscuredContentRect.maxY() > newContentSize.height())
            verticalAdjustment -= newUnobscuredContentRect.maxY() - newContentSize.height();
        if (newUnobscuredContentRect.x() < 0)
            horizontalAdjustment += - newUnobscuredContentRect.x();
        if (newUnobscuredContentRect.y() < 0)
            verticalAdjustment += - newUnobscuredContentRect.y();

        FloatPoint adjustmentDelta(horizontalAdjustment, verticalAdjustment);
        newUnobscuredContentRect.moveBy(adjustmentDelta);
        newExposedContentRect.moveBy(adjustmentDelta);
    }

    frameView.setScrollVelocity({ 0, 0, 0, MonotonicTime::now() });

    IntPoint roundedUnobscuredContentRectPosition = roundedIntPoint(newUnobscuredContentRect.location());
    frameView.setUnobscuredContentSize(newUnobscuredContentRect.size());
    m_drawingArea->setExposedContentRect(newExposedContentRect);

    scalePage(scale, roundedUnobscuredContentRectPosition);

    frameView.updateLayoutAndStyleIfNeededRecursive();

    // FIXME: Move settings from Frame to Frame and remove this check.
    auto& settings = frameView.frame().settings();
    LayoutRect documentRect = IntRect(frameView.scrollOrigin(), frameView.contentsSize());
    double heightExpansionFactor = m_disallowLayoutViewportHeightExpansionReasons.isEmpty() ? settings.layoutViewportHeightExpansionFactor() : 0;
    auto layoutViewportSize = LocalFrameView::expandedLayoutViewportSize(frameView.baseLayoutViewportSize(), LayoutSize(documentRect.size()), heightExpansionFactor);
    LayoutRect layoutViewportRect = LocalFrameView::computeUpdatedLayoutViewportRect(frameView.layoutViewportRect(), documentRect, LayoutSize(newUnobscuredContentRect.size()), LayoutRect(newUnobscuredContentRect), layoutViewportSize, frameView.minStableLayoutViewportOrigin(), frameView.maxStableLayoutViewportOrigin(), LayoutViewportConstraint::ConstrainedToDocumentRect);
    frameView.setLayoutViewportOverrideRect(layoutViewportRect);
    frameView.layoutOrVisualViewportChanged();

    frameView.setCustomSizeForResizeEvent(expandedIntSize(target.unobscuredRectInScrollViewCoordinates.size()));
    setDeviceOrientation(target.deviceOrientation);
    frameView.setScrollOffset(roundedUnobscuredContentRectPosition);

    m_page->isolatedUpdateRendering();

#if ENABLE(VIEWPORT_RESIZING)
    shrinkToFitContent();
#endif

    m_drawingArea->triggerRenderingUpdate();

    m_pendingDynamicViewportSizeUpdateID = target.identifier;
}

void WebPage::resetViewportDefaultConfiguration(WebFrame* frame, bool hasMobileDocType)
{
    LOG_WITH_STREAM(VisibleRects, stream << "WebPage " << m_identifier << " resetViewportDefaultConfiguration");
    if (m_useTestingViewportConfiguration) {
        m_viewportConfiguration.setDefaultConfiguration(ViewportConfiguration::testingParameters());
        return;
    }

    auto parametersForStandardFrame = [&] {
#if ENABLE(FULLSCREEN_API)
        if (m_isInFullscreenMode == IsInFullscreenMode::Yes)
            return m_viewportConfiguration.nativeWebpageParameters();
#endif
        if (shouldIgnoreMetaViewport())
            return m_viewportConfiguration.nativeWebpageParameters();
        return ViewportConfiguration::webpageParameters();
    };

    RefPtr localFrame = frame->coreLocalFrame() ? frame->coreLocalFrame() : frame->provisionalFrame();
    ASSERT(localFrame);
    if (!frame || !localFrame) {
        m_viewportConfiguration.setDefaultConfiguration(parametersForStandardFrame());
        return;
    }

    RefPtr document = localFrame->document();

    auto updateViewportConfigurationForMobileDocType = [this, document] {
        m_viewportConfiguration.setDefaultConfiguration(ViewportConfiguration::xhtmlMobileParameters());

        // Do not update the viewport arguments if they are already configured by the website.
        if (m_viewportConfiguration.viewportArguments().type == ViewportArguments::Type::ViewportMeta)
            return;

        if (!document || !document->isViewportDocument())
            return;

        // https://www.w3.org/TR/2016/WD-css-device-adapt-1-20160329/#intro
        // Certain DOCTYPEs (for instance XHTML Mobile Profile) are used to recognize mobile documents which are assumed
        // to be designed for handheld devices, hence using the viewport size as the initial containing block size.
        ViewportArguments viewportArguments { ViewportArguments::Type::ViewportMeta };
        viewportArguments.width = ViewportArguments::ValueDeviceWidth;
        viewportArguments.height = ViewportArguments::ValueDeviceHeight;
        viewportArguments.zoom = 1;
        document->setViewportArguments(viewportArguments);
        viewportPropertiesDidChange(viewportArguments);
    };

    if (hasMobileDocType) {
        return updateViewportConfigurationForMobileDocType();
    }

    bool configureWithParametersForStandardFrame = !document;

    if (document) {
        if (document->isImageDocument())
            m_viewportConfiguration.setDefaultConfiguration(ViewportConfiguration::imageDocumentParameters());
        else if (document->isTextDocument())
            m_viewportConfiguration.setDefaultConfiguration(ViewportConfiguration::textDocumentParameters());
#if ENABLE(PDF_PLUGIN)
        else if (m_page->settings().unifiedPDFEnabled() && document->isPluginDocument())
            m_viewportConfiguration.setDefaultConfiguration(UnifiedPDFPlugin::viewportParameters());
#endif
        else
            configureWithParametersForStandardFrame = true;
    }

    if (configureWithParametersForStandardFrame)
        return m_viewportConfiguration.setDefaultConfiguration(parametersForStandardFrame());
}

#if ENABLE(TEXT_AUTOSIZING)

void WebPage::updateTextAutosizingEnablementFromInitialScale(double initialScale)
{
    if (m_page->settings().textAutosizingEnabledAtLargeInitialScale())
        return;

    bool shouldEnable = initialScale <= 1;
    if (shouldEnable == m_page->settings().textAutosizingEnabled())
        return;

    m_page->settings().setTextAutosizingEnabled(shouldEnable);
}

void WebPage::resetIdempotentTextAutosizingIfNeeded(double previousInitialScale)
{
    if (!m_page->settings().textAutosizingEnabled() || !m_page->settings().textAutosizingUsesIdempotentMode())
        return;

    const float minimumScaleChangeBeforeRecomputingTextAutosizing = 0.01;
    if (std::abs(previousInitialScale - m_page->initialScaleIgnoringContentSize()) < minimumScaleChangeBeforeRecomputingTextAutosizing)
        return;

    if (m_page->initialScaleIgnoringContentSize() >= 1 && previousInitialScale >= 1)
        return;
    
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;

    if (!localMainFrame->view())
        return;

    auto textAutoSizingDelay = [&] {
        auto& frameView = *localMainFrame->view();
        if (!frameView.isVisuallyNonEmpty()) {
            // We don't anticipate any painting after the next upcoming layout.
            const Seconds longTextAutoSizingDelayOnViewportChange = 100_ms;
            return longTextAutoSizingDelayOnViewportChange;
        }
        const Seconds defaultTextAutoSizingDelayOnViewportChange = 80_ms;
        return defaultTextAutoSizingDelayOnViewportChange;
    };

    // We don't need to update text sizing eagerly. There might be multiple incoming dynamic viewport changes.
    m_textAutoSizingAdjustmentTimer.startOneShot(textAutoSizingDelay());
}
#endif // ENABLE(TEXT_AUTOSIZING)

void WebPage::resetTextAutosizing()
{
#if ENABLE(TEXT_AUTOSIZING)
    for (auto* frame = &m_page->mainFrame(); frame; frame = frame->tree().traverseNext()) {
        auto* localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        auto* document = localFrame->document();
        if (!document || !document->renderView())
            continue;
        document->renderView()->resetTextAutosizing();
    }
#endif
}

#if ENABLE(VIEWPORT_RESIZING)

void WebPage::shrinkToFitContent(ZoomToInitialScale zoomToInitialScale)
{
    if (m_isClosed)
        return;

    if (!m_page->settings().allowViewportShrinkToFitContent())
        return;

    if (m_useTestingViewportConfiguration)
        return;

    if (!shouldIgnoreMetaViewport())
        return;

    if (!m_viewportConfiguration.viewportArguments().shrinkToFit)
        return;

    if (m_viewportConfiguration.canIgnoreScalingConstraints())
        return;

    RefPtr mainFrame = m_mainFrame->coreLocalFrame();
    if (!mainFrame)
        return;

    RefPtr view = mainFrame->view();
    RefPtr mainDocument = mainFrame->document();
    if (!view || !mainDocument)
        return;

    mainDocument->updateLayout();

    static const int toleratedHorizontalScrollingDistance = 20;
    static const int maximumExpandedLayoutWidth = 1280;
    static const int maximumContentWidthBeforeAvoidingShrinkToFit = 1920;

    auto scaledViewWidth = [&] () -> int {
        return std::round(m_viewportConfiguration.viewLayoutSize().width() / m_viewportConfiguration.initialScale());
    };

    int originalContentWidth = view->contentsWidth();
    int originalViewWidth = scaledViewWidth();
    int originalLayoutWidth = m_viewportConfiguration.layoutWidth();
    int originalHorizontalOverflowAmount = originalContentWidth - originalViewWidth;
    if (originalHorizontalOverflowAmount <= toleratedHorizontalScrollingDistance || originalLayoutWidth >= maximumExpandedLayoutWidth || originalContentWidth <= originalViewWidth || originalContentWidth > maximumContentWidthBeforeAvoidingShrinkToFit)
        return;

    auto changeMinimumEffectiveDeviceWidth = [this, mainDocument] (int targetLayoutWidth) -> bool {
        if (m_viewportConfiguration.setMinimumEffectiveDeviceWidthForShrinkToFit(targetLayoutWidth)) {
            viewportConfigurationChanged();
            mainDocument->updateLayout();
            return true;
        }
        return false;
    };

    m_viewportConfiguration.setIsKnownToLayOutWiderThanViewport(true);
    double originalMinimumDeviceWidth = m_viewportConfiguration.minimumEffectiveDeviceWidth();
    if (changeMinimumEffectiveDeviceWidth(std::min(maximumExpandedLayoutWidth, originalContentWidth)) && view->contentsWidth() - scaledViewWidth() > originalHorizontalOverflowAmount) {
        changeMinimumEffectiveDeviceWidth(originalMinimumDeviceWidth);
        m_viewportConfiguration.setIsKnownToLayOutWiderThanViewport(false);
    }

    // FIXME (197429): Consider additionally logging an error message to the console if a responsive meta viewport tag was used.
    RELEASE_LOG(ViewportSizing, "Shrink-to-fit: content width %d => %d; layout width %d => %d", originalContentWidth, view->contentsWidth(), originalLayoutWidth, m_viewportConfiguration.layoutWidth());
    viewportConfigurationChanged(zoomToInitialScale);
}

#endif // ENABLE(VIEWPORT_RESIZING)

bool WebPage::shouldIgnoreMetaViewport() const
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (auto* mainDocument = localMainFrame ? localMainFrame->document() : nullptr) {
        auto* loader = mainDocument->loader();
        if (loader && loader->metaViewportPolicy() == WebCore::MetaViewportPolicy::Ignore)
            return true;
    }
    return m_page->settings().shouldIgnoreMetaViewport();
}

bool WebPage::mainFramePlugInDefersScalingToViewport() const
{
#if ENABLE(PDF_PLUGIN)
    if (RefPtr plugin = mainFramePlugIn(); plugin && !plugin->pluginHandlesPageScaleFactor())
        return true;
#endif
    return false;
}

bool WebPage::mainFramePlugInRejectsZeroViewLayoutSizeUpdates() const
{
#if ENABLE(PDF_PLUGIN)
    if (RefPtr plugin = mainFramePlugIn(); plugin && !plugin->pluginHandlesPageScaleFactor())
        return true;
#endif
    return false;
}

bool WebPage::shouldEnableViewportBehaviorsForResizableWindows() const
{
#if HAVE(UIKIT_RESIZABLE_WINDOWS)
    return shouldIgnoreMetaViewport() && m_isWindowResizingEnabled && !mainFramePlugInDefersScalingToViewport();
#else
    return false;
#endif
}

void WebPage::viewportConfigurationChanged(ZoomToInitialScale zoomToInitialScale)
{
    double initialScale = m_viewportConfiguration.initialScale();
    double initialScaleIgnoringContentSize = m_viewportConfiguration.initialScaleIgnoringContentSize();
#if ENABLE(TEXT_AUTOSIZING)
    double previousInitialScaleIgnoringContentSize = m_page->initialScaleIgnoringContentSize();
    m_page->setInitialScaleIgnoringContentSize(initialScaleIgnoringContentSize);
    resetIdempotentTextAutosizingIfNeeded(previousInitialScaleIgnoringContentSize);
    updateTextAutosizingEnablementFromInitialScale(initialScale);
#endif
    if (setFixedLayoutSize(m_viewportConfiguration.layoutSize()))
        resetTextAutosizing();

    double scale;
    if (m_userHasChangedPageScaleFactor && zoomToInitialScale == ZoomToInitialScale::No)
        scale = std::max(std::min(pageScaleFactor(), m_viewportConfiguration.maximumScale()), m_viewportConfiguration.minimumScale());
    else
        scale = initialScale;

    LOG_WITH_STREAM(VisibleRects, stream << "WebPage " << m_identifier << " viewportConfigurationChanged - setting zoomedOutPageScaleFactor to " << m_viewportConfiguration.minimumScale() << " and scale to " << scale);

    m_page->setZoomedOutPageScaleFactor(m_viewportConfiguration.minimumScale());

    updateSizeForCSSDefaultViewportUnits();
    updateSizeForCSSSmallViewportUnits();
    updateSizeForCSSLargeViewportUnits();

    auto* mainFrameView = this->localMainFrameView();
    if (!mainFrameView) {
        // FIXME: This is hit in some site isolation tests on iOS. Investigate and fix. <rdar://116201382>
        return;
    }

    auto& frameView = *mainFrameView;
    IntPoint scrollPosition = frameView.scrollPosition();
    if (!m_hasReceivedVisibleContentRectsAfterDidCommitLoad) {
        FloatSize minimumLayoutSizeInScrollViewCoordinates = m_viewportConfiguration.viewLayoutSize();
        minimumLayoutSizeInScrollViewCoordinates.scale(1 / scale);
        IntSize minimumLayoutSizeInDocumentCoordinates = roundedIntSize(minimumLayoutSizeInScrollViewCoordinates);
        frameView.setUnobscuredContentSize(minimumLayoutSizeInDocumentCoordinates);
        frameView.setScrollVelocity({ 0, 0, 0, MonotonicTime::now() });

        // FIXME: We could send down the obscured margins to find a better exposed rect and unobscured rect.
        // It is not a big deal at the moment because the tile coverage will always extend past the obscured bottom inset.
        if (!m_hasRestoredExposedContentRectAfterDidCommitLoad)
            m_drawingArea->setExposedContentRect(FloatRect(scrollPosition, minimumLayoutSizeInDocumentCoordinates));
    }
    scalePage(scale, scrollPosition);
    
    if (!m_hasReceivedVisibleContentRectsAfterDidCommitLoad) {
        // This takes scale into account, so do after the scale change.
        frameView.setCustomFixedPositionLayoutRect(enclosingIntRect(frameView.viewportConstrainedObjectsRect()));

        frameView.setCustomSizeForResizeEvent(expandedIntSize(m_viewportConfiguration.minimumLayoutSize()));
    }
}

void WebPage::applicationWillResignActive()
{
    [[NSNotificationCenter defaultCenter] postNotificationName:WebUIApplicationWillResignActiveNotification object:nil];

    // FIXME(224775): Move to WebProcess
    if (auto* manager = PlatformMediaSessionManager::singletonIfExists())
        manager->applicationWillBecomeInactive();

    if (m_page)
        m_page->applicationWillResignActive();
}

void WebPage::applicationDidEnterBackground(bool isSuspendedUnderLock)
{
    [[NSNotificationCenter defaultCenter] postNotificationName:WebUIApplicationDidEnterBackgroundNotification object:nil userInfo:@{@"isSuspendedUnderLock": @(isSuspendedUnderLock)}];

    m_isSuspendedUnderLock = isSuspendedUnderLock;
    freezeLayerTree(LayerTreeFreezeReason::BackgroundApplication);

    // FIXME(224775): Move to WebProcess
    if (auto* manager = PlatformMediaSessionManager::singletonIfExists())
        manager->applicationDidEnterBackground(isSuspendedUnderLock);

    if (m_page)
        m_page->applicationDidEnterBackground();
}

void WebPage::applicationDidFinishSnapshottingAfterEnteringBackground()
{
    markLayersVolatile();
}

void WebPage::applicationWillEnterForeground(bool isSuspendedUnderLock)
{
    m_isSuspendedUnderLock = false;
    cancelMarkLayersVolatile();

    unfreezeLayerTree(LayerTreeFreezeReason::BackgroundApplication);

    [[NSNotificationCenter defaultCenter] postNotificationName:WebUIApplicationWillEnterForegroundNotification object:nil userInfo:@{@"isSuspendedUnderLock": @(isSuspendedUnderLock)}];

    // FIXME(224775): Move to WebProcess
    if (auto* manager = PlatformMediaSessionManager::singletonIfExists())
        manager->applicationWillEnterForeground(isSuspendedUnderLock);

    if (m_page)
        m_page->applicationWillEnterForeground();
}

void WebPage::applicationDidBecomeActive()
{
    [[NSNotificationCenter defaultCenter] postNotificationName:WebUIApplicationDidBecomeActiveNotification object:nil];

    // FIXME(224775): Move to WebProcess
    if (auto* manager = PlatformMediaSessionManager::singletonIfExists())
        manager->applicationDidBecomeActive();

    if (m_page)
        m_page->applicationDidBecomeActive();
}

void WebPage::applicationDidEnterBackgroundForMedia(bool isSuspendedUnderLock)
{
    if (auto* manager = PlatformMediaSessionManager::singletonIfExists())
        manager->applicationDidEnterBackground(isSuspendedUnderLock);
}

void WebPage::applicationWillEnterForegroundForMedia(bool isSuspendedUnderLock)
{
    if (auto* manager = PlatformMediaSessionManager::singletonIfExists())
        manager->applicationWillEnterForeground(isSuspendedUnderLock);
}

static inline void adjustVelocityDataForBoundedScale(VelocityData& velocityData, double exposedRectScale, double minimumScale, double maximumScale)
{
    if (velocityData.scaleChangeRate) {
        velocityData.horizontalVelocity = 0;
        velocityData.verticalVelocity = 0;
    }

    if (exposedRectScale >= maximumScale || exposedRectScale <= minimumScale || scalesAreEssentiallyEqual(exposedRectScale, minimumScale) || scalesAreEssentiallyEqual(exposedRectScale, maximumScale))
        velocityData.scaleChangeRate = 0;
}

std::optional<float> WebPage::scaleFromUIProcess(const VisibleContentRectUpdateInfo& visibleContentRectUpdateInfo) const
{
    auto transactionIDForLastScaleFromUIProcess = visibleContentRectUpdateInfo.lastLayerTreeTransactionID();
    if (m_internals->lastTransactionIDWithScaleChange && m_internals->lastTransactionIDWithScaleChange->greaterThanSameProcess(transactionIDForLastScaleFromUIProcess))
        return std::nullopt;

    float scaleFromUIProcess = visibleContentRectUpdateInfo.scale();
    float currentScale = m_page->pageScaleFactor();

    double scaleNoiseThreshold = 0.005;
    if (!m_isInStableState && std::abs(scaleFromUIProcess - currentScale) < scaleNoiseThreshold) {
        // Tiny changes of scale during interactive zoom cause content to jump by one pixel, creating
        // visual noise. We filter those useless updates.
        scaleFromUIProcess = currentScale;
    }
    
    scaleFromUIProcess = std::min<float>(m_viewportConfiguration.maximumScale(), std::max<float>(m_viewportConfiguration.minimumScale(), scaleFromUIProcess));
    if (scalesAreEssentiallyEqual(currentScale, scaleFromUIProcess))
        return std::nullopt;

    return scaleFromUIProcess;
}

static bool selectionIsInsideFixedPositionContainer(LocalFrame& frame)
{
    auto& selection = frame.selection().selection();
    if (selection.isNone())
        return false;

    bool isInsideFixedPosition = false;
    if (selection.isCaret()) {
        frame.selection().absoluteCaretBounds(&isInsideFixedPosition);
        return isInsideFixedPosition;
    }

    selection.visibleStart().absoluteCaretBounds(&isInsideFixedPosition);
    if (isInsideFixedPosition)
        return true;

    selection.visibleEnd().absoluteCaretBounds(&isInsideFixedPosition);
    return isInsideFixedPosition;
}

void WebPage::updateVisibleContentRects(const VisibleContentRectUpdateInfo& visibleContentRectUpdateInfo, MonotonicTime oldestTimestamp)
{
    LOG_WITH_STREAM(VisibleRects, stream << "\nWebPage " << m_identifier << " updateVisibleContentRects " << visibleContentRectUpdateInfo);

    // Skip any VisibleContentRectUpdate that have been queued before DidCommitLoad suppresses the updates in the UIProcess.
    if (m_mainFrame->firstLayerTreeTransactionIDAfterDidCommitLoad() && visibleContentRectUpdateInfo.lastLayerTreeTransactionID().lessThanSameProcess(*m_mainFrame->firstLayerTreeTransactionIDAfterDidCommitLoad()) && !visibleContentRectUpdateInfo.isFirstUpdateForNewViewSize())
        return;

    m_hasReceivedVisibleContentRectsAfterDidCommitLoad = true;
    m_isInStableState = visibleContentRectUpdateInfo.inStableState();

    auto scaleFromUIProcess = this->scaleFromUIProcess(visibleContentRectUpdateInfo);

    // Skip progressively redrawing tiles if pinch-zooming while the system is under memory pressure.
    if (scaleFromUIProcess && !m_isInStableState && MemoryPressureHandler::singleton().isUnderMemoryPressure())
        return;

    if (m_isInStableState)
        m_hasStablePageScaleFactor = true;
    else {
        if (!m_oldestNonStableUpdateVisibleContentRectsTimestamp)
            m_oldestNonStableUpdateVisibleContentRectsTimestamp = oldestTimestamp;
    }

    float scaleToUse = scaleFromUIProcess.value_or(m_page->pageScaleFactor());
    FloatRect exposedContentRect = visibleContentRectUpdateInfo.exposedContentRect();
    FloatRect adjustedExposedContentRect = adjustExposedRectForNewScale(exposedContentRect, visibleContentRectUpdateInfo.scale(), scaleToUse);
    m_drawingArea->setExposedContentRect(adjustedExposedContentRect);
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;
    auto& frameView = *localMainFrame->view();

    if (auto* scrollingCoordinator = this->scrollingCoordinator()) {
        auto& remoteScrollingCoordinator = downcast<RemoteScrollingCoordinator>(*scrollingCoordinator);
        if (auto mainFrameScrollingNodeID = frameView.scrollingNodeID()) {
            if (visibleContentRectUpdateInfo.viewStability().contains(ViewStabilityFlag::ScrollViewRubberBanding))
                remoteScrollingCoordinator.addNodeWithActiveRubberBanding(*mainFrameScrollingNodeID);
            else
                remoteScrollingCoordinator.removeNodeWithActiveRubberBanding(*mainFrameScrollingNodeID);
        }
    }

    auto layoutViewportRect = visibleContentRectUpdateInfo.layoutViewportRect();
    auto unobscuredContentRect = visibleContentRectUpdateInfo.unobscuredContentRect();
    auto scrollPosition = roundedIntPoint(unobscuredContentRect.location());

    // Computation of layoutViewportRect is done in LayoutUnits which loses some precision, so test with an epsilon.
    constexpr auto epsilon = 2.0f / kFixedPointDenominator;
    if (areEssentiallyEqual(unobscuredContentRect.location(), layoutViewportRect.location(), epsilon))
        layoutViewportRect.setLocation(scrollPosition);

    bool pageHasBeenScaledSinceLastLayerTreeCommitThatChangedPageScale = ([&] {
        if (!m_internals->lastLayerTreeTransactionIdAndPageScaleBeforeScalingPage)
            return false;

        if (scalesAreEssentiallyEqual(scaleToUse, m_page->pageScaleFactor()))
            return false;

        auto [transactionIdBeforeScalingPage, scaleBeforeScalingPage] = *m_internals->lastLayerTreeTransactionIdAndPageScaleBeforeScalingPage;
        if (!scalesAreEssentiallyEqual(scaleBeforeScalingPage, scaleToUse))
            return false;

        return transactionIdBeforeScalingPage.greaterThanOrEqualSameProcess( visibleContentRectUpdateInfo.lastLayerTreeTransactionID());
    })();

    if (!pageHasBeenScaledSinceLastLayerTreeCommitThatChangedPageScale) {
        bool shouldSetCorePageScale = [this, protectedThis = Ref { *this }] {
#if ENABLE(PDF_PLUGIN)
            RefPtr pluginView = mainFramePlugIn();
            if (!pluginView)
                return true;
            return !pluginView->pluginHandlesPageScaleFactor();
#else
            UNUSED_PARAM(this);
            return true;
#endif
        }();

        auto setCorePageScaleFactor = [this, protectedThis = Ref { *this }](float scale, const auto& origin, bool inStableState) {
            m_page->setPageScaleFactor(scale, origin, inStableState);
#if ENABLE(PDF_PLUGIN)
            if (RefPtr pluginView = mainFramePlugIn())
                pluginView->mainFramePageScaleFactorDidChange();
#endif
        };

        bool hasSetPageScale = false;
        if (scaleFromUIProcess) {
            m_scaleWasSetByUIProcess = true;
            m_hasStablePageScaleFactor = m_isInStableState;

            m_internals->dynamicSizeUpdateHistory.clear();

            if (shouldSetCorePageScale)
                setCorePageScaleFactor(scaleFromUIProcess.value(), scrollPosition, m_isInStableState);

            hasSetPageScale = true;
            send(Messages::WebPageProxy::PageScaleFactorDidChange(scaleFromUIProcess.value()));
        }

        if (!hasSetPageScale && m_isInStableState && shouldSetCorePageScale)
            setCorePageScaleFactor(scaleToUse, scrollPosition, true);
    }

    if (scrollPosition != frameView.scrollPosition())
        m_internals->dynamicSizeUpdateHistory.clear();

    if (m_viewportConfiguration.setCanIgnoreScalingConstraints(visibleContentRectUpdateInfo.allowShrinkToFit()))
        viewportConfigurationChanged();

    double minimumEffectiveDeviceWidthWhenIgnoringScalingConstraints = ([&] {
        RefPtr document = localMainFrame->document();
        if (!document)
            return 0;

        if (!document->quirks().shouldLayOutAtMinimumWindowWidthWhenIgnoringScalingConstraints())
            return 0;

        // This value is chosen to be close to the minimum width of a Safari window on macOS.
        return 500;
    })();

    if (m_viewportConfiguration.setMinimumEffectiveDeviceWidthWhenIgnoringScalingConstraints(minimumEffectiveDeviceWidthWhenIgnoringScalingConstraints))
        viewportConfigurationChanged();

    frameView.clearObscuredInsetsAdjustmentsIfNeeded();
    frameView.setUnobscuredContentSize(unobscuredContentRect.size());
    m_page->setContentInsets(visibleContentRectUpdateInfo.contentInsets());
    m_page->setObscuredInsets(visibleContentRectUpdateInfo.obscuredInsets());
    m_page->setUnobscuredSafeAreaInsets(visibleContentRectUpdateInfo.unobscuredSafeAreaInsets());
    m_page->setEnclosedInScrollableAncestorView(visibleContentRectUpdateInfo.enclosedInScrollableAncestorView());

    VelocityData scrollVelocity = visibleContentRectUpdateInfo.scrollVelocity();
    adjustVelocityDataForBoundedScale(scrollVelocity, visibleContentRectUpdateInfo.scale(), m_viewportConfiguration.minimumScale(), m_viewportConfiguration.maximumScale());
    frameView.setScrollVelocity(scrollVelocity);

    bool visualViewportChanged = unobscuredContentRect != visibleContentRectUpdateInfo.unobscuredContentRectRespectingInputViewBounds();
    if (visualViewportChanged)
        frameView.setVisualViewportOverrideRect(LayoutRect(visibleContentRectUpdateInfo.unobscuredContentRectRespectingInputViewBounds()));
    else if (m_isInStableState) {
        frameView.setVisualViewportOverrideRect(std::nullopt);
        visualViewportChanged = true;
    }

    bool isChangingObscuredInsetsInteractively = visibleContentRectUpdateInfo.viewStability().contains(ViewStabilityFlag::ChangingObscuredInsetsInteractively);
    bool shouldPerformLayout = m_isInStableState && !isChangingObscuredInsetsInteractively;

    LOG_WITH_STREAM(VisibleRects, stream << "WebPage::updateVisibleContentRects - setLayoutViewportOverrideRect " << layoutViewportRect);
    frameView.setLayoutViewportOverrideRect(LayoutRect(layoutViewportRect), shouldPerformLayout ? LocalFrameView::TriggerLayoutOrNot::Yes : LocalFrameView::TriggerLayoutOrNot::No);

    if (m_isInStableState) {
        if (selectionIsInsideFixedPositionContainer(*localMainFrame)) {
            // Ensure that the next layer tree commit contains up-to-date caret/selection rects.
            frameView.frame().selection().setCaretRectNeedsUpdate();
            scheduleFullEditorStateUpdate();
        }
    }

    if (visualViewportChanged)
        frameView.layoutOrVisualViewportChanged();

    if (!isChangingObscuredInsetsInteractively)
        frameView.setCustomSizeForResizeEvent(expandedIntSize(visibleContentRectUpdateInfo.unobscuredRectInScrollViewCoordinates().size()));

    if (auto* scrollingCoordinator = this->scrollingCoordinator()) {
        auto viewportStability = ViewportRectStability::Stable;
        auto layerAction = ScrollingLayerPositionAction::Sync;
        
        if (isChangingObscuredInsetsInteractively) {
            viewportStability = ViewportRectStability::ChangingObscuredInsetsInteractively;
            layerAction = ScrollingLayerPositionAction::SetApproximate;
        } else if (!m_isInStableState) {
            viewportStability = ViewportRectStability::Unstable;
            layerAction = ScrollingLayerPositionAction::SetApproximate;
        }
        scrollingCoordinator->reconcileScrollingState(frameView, scrollPosition, visibleContentRectUpdateInfo.layoutViewportRect(), ScrollType::User, viewportStability, layerAction);
    }
}

void WebPage::scheduleLayoutViewportHeightExpansionUpdate()
{
    if (!m_page->settings().layoutViewportHeightExpansionFactor())
        return;

    if (m_updateLayoutViewportHeightExpansionTimer.isActive()) {
        m_shouldRescheduleLayoutViewportHeightExpansionTimer = true;
        return;
    }

    m_updateLayoutViewportHeightExpansionTimer.restart();
}

void WebPage::updateLayoutViewportHeightExpansionTimerFired()
{
    if (m_disallowLayoutViewportHeightExpansionReasons.contains(DisallowLayoutViewportHeightExpansionReason::LargeContainer))
        return;

    RefPtr mainFrame = m_mainFrame->coreLocalFrame();
    if (!mainFrame)
        return;

    RefPtr document = mainFrame->document();
    if (!document)
        return;

    RefPtr view = mainFrame->view();
    if (!view)
        return;

    FloatRect viewportRect = view->viewportConstrainedObjectsRect();

    bool hitTestedToLargeViewportConstrainedElement = [&] {
        if (!view->hasViewportConstrainedObjects())
            return false;

        HashSet<Ref<Element>> largeViewportConstrainedElements;
        for (auto& renderer : *view->viewportConstrainedObjects()) {
            RefPtr element = renderer.element();
            if (!element)
                continue;

            auto bounds = renderer.absoluteBoundingBoxRect();
            if (intersection(viewportRect, bounds).height() > 0.9 * viewportRect.height())
                largeViewportConstrainedElements.add(element.releaseNonNull());
        }

        if (largeViewportConstrainedElements.isEmpty())
            return false;

        auto hitTestResult = HitTestResult { LayoutRect { viewportRect } };
        if (!document->hitTest({ HitTestSource::User, { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::CollectMultipleElements } }, hitTestResult))
            return false;

        auto& hitTestedNodes = hitTestResult.listBasedTestResult();
        HashSet<Ref<Element>> elementsOutsideOfAnyLargeViewportConstrainedContainers;
        for (auto& node : hitTestedNodes) {
            RefPtr firstParentOrSelf = dynamicDowncast<Element>(node) ?: node->parentElementInComposedTree();
            Vector<Ref<Element>> ancestorsForHitTestedNode;
            for (RefPtr parent = firstParentOrSelf; parent; parent = parent->parentElementInComposedTree()) {
                if (largeViewportConstrainedElements.contains(*parent))
                    return true;

                if (elementsOutsideOfAnyLargeViewportConstrainedContainers.contains(*parent))
                    break;

                ancestorsForHitTestedNode.append(*parent);
            }
            for (auto& ancestor : ancestorsForHitTestedNode)
                elementsOutsideOfAnyLargeViewportConstrainedContainers.add(ancestor);
        }
        return false;
    }();

    if (hitTestedToLargeViewportConstrainedElement) {
        RELEASE_LOG(ViewportSizing, "Shrinking viewport down to normal height (found large fixed-position container)");
        addReasonsToDisallowLayoutViewportHeightExpansion(DisallowLayoutViewportHeightExpansionReason::LargeContainer);
    } else if (m_shouldRescheduleLayoutViewportHeightExpansionTimer) {
        m_shouldRescheduleLayoutViewportHeightExpansionTimer = false;
        m_updateLayoutViewportHeightExpansionTimer.restart();
    }
}

void WebPage::willStartUserTriggeredZooming()
{
#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = mainFramePlugIn())
        pluginView->didBeginMagnificationGesture();
#endif

    m_page->diagnosticLoggingClient().logDiagnosticMessage(DiagnosticLoggingKeys::webViewKey(), DiagnosticLoggingKeys::userZoomActionKey(), ShouldSample::No);
    m_userHasChangedPageScaleFactor = true;
}

void WebPage::didEndUserTriggeredZooming()
{
#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = mainFramePlugIn())
        pluginView->didEndMagnificationGesture();
#endif
}

#if ENABLE(IOS_TOUCH_EVENTS)
static std::optional<RemoteWebTouchEvent> transformEventIfNecessary(const Expected<bool, WebCore::RemoteFrameGeometryTransformer>& transformer, WebTouchEvent&& event)
{
    if (transformer)
        return std::nullopt;
    event.transformToRemoteFrameCoordinates(transformer.error());
    return RemoteWebTouchEvent { transformer.error().remoteFrameID(), WTFMove(event) };
}

void WebPage::dispatchAsynchronousTouchEvents(UniqueRef<EventDispatcher::TouchEventQueue>&& queue)
{
    for (auto& touchEventData : queue.get()) {
        auto handleTouchEventResult = dispatchTouchEvent(touchEventData.frameID, touchEventData.event);
        if (auto& completionHandler = touchEventData.completionHandler)
            completionHandler(handleTouchEventResult.value_or(false), transformEventIfNecessary(handleTouchEventResult, WTFMove(touchEventData.event)));
    }
}

void WebPage::cancelAsynchronousTouchEvents(UniqueRef<EventDispatcher::TouchEventQueue>&& queue)
{
    for (auto& touchEventData : queue.get()) {
        if (auto& completionHandler = touchEventData.completionHandler)
            completionHandler(true, std::nullopt);
    }
}
#endif

void WebPage::computePagesForPrintingiOS(WebCore::FrameIdentifier frameID, const PrintInfo& printInfo, CompletionHandler<void(uint64_t)>&& reply)
{
    ASSERT_WITH_MESSAGE(!printInfo.snapshotFirstPage, "If we are just snapshotting the first page, we don't need a synchronous message to determine the page count, which is 1.");

    Vector<WebCore::IntRect> pageRects;
    double totalScaleFactor;
    auto margin = printInfo.margin;
    computePagesForPrintingImpl(frameID, printInfo, pageRects, totalScaleFactor, margin);

    RELEASE_LOG(Printing, "Computing pages for printing. Page rects size = %zu", pageRects.size());

    ASSERT(pageRects.size() >= 1);
    reply(pageRects.size());
}

void WebPage::drawToImage(WebCore::FrameIdentifier frameID, const PrintInfo& printInfo, CompletionHandler<void(std::optional<WebCore::ShareableBitmap::Handle>&&)>&& reply)
{  
    Vector<WebCore::IntRect> pageRects;
    double totalScaleFactor;
    auto margin = printInfo.margin;
    computePagesForPrintingImpl(frameID, printInfo, pageRects, totalScaleFactor, margin);

    size_t pageCount = pageRects.size();

    RELEASE_LOG(Printing, "Drawing to image. Page rects size = %zu", pageCount);

    ASSERT(pageCount >= 1);

    if (!m_printContext) {
        reply({ });
        endPrinting();
        return;
    }

    Checked<int> pageWidth = pageRects[0].width();
    Checked<int> pageHeight = pageRects[0].height();

    // The thumbnail images are always a maximum of 500 x 500.
    static constexpr float maximumPrintPreviewDimensionSize = 500.0;

    // If the sizes are too large, the bitmap will not be able to be created,
    // so scale them down.
    float scaleFactor = maximumPrintPreviewDimensionSize / static_cast<int>(std::max(pageWidth, pageHeight));
    if (scaleFactor < 1.0) {
        pageWidth = static_cast<int>(std::floorf(static_cast<int>(pageWidth) * scaleFactor));
        pageHeight = static_cast<int>(std::floorf(static_cast<int>(pageHeight) * scaleFactor));
    }

    int imageHeight;
    if (!WTF::safeMultiply(pageHeight.value<size_t>(), pageCount, imageHeight)) {
        reply({ });
        endPrinting();
        return;
    }

    auto bitmap = ShareableBitmap::create({ IntSize(pageWidth, imageHeight) });
    if (!bitmap) {
        reply({ });
        endPrinting();
        return;
    }

    auto graphicsContext = bitmap->createGraphicsContext();
    if (!graphicsContext) {
        reply({ });
        endPrinting();
        return;
    }

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        if (pageIndex >= m_printContext->pageCount())
            break;
        graphicsContext->save();
        graphicsContext->translate(0, pageHeight * static_cast<int>(pageIndex));
        m_printContext->spoolPage(*graphicsContext, pageIndex, pageWidth);
        graphicsContext->restore();
    }

    auto handle = bitmap->createHandle(SharedMemory::Protection::ReadOnly);
    reply(WTFMove(handle));
    endPrinting();
}

void WebPage::drawToPDFiOS(FrameIdentifier frameID, const PrintInfo& printInfo, uint64_t pageCount, CompletionHandler<void(RefPtr<SharedBuffer>&&)>&& reply)
{
    if (printInfo.snapshotFirstPage) {
        RefPtr localMainFrame = m_page->localMainFrame();
        if (!localMainFrame)
            return;

        auto snapshotRect = IntRect { FloatRect { { }, FloatSize { printInfo.availablePaperWidth, printInfo.availablePaperHeight } } };

        RefPtr buffer = ImageBuffer::create(snapshotRect.size(), RenderingMode::PDFDocument, RenderingPurpose::Snapshot, 1, DestinationColorSpace::SRGB(), ImageBufferPixelFormat::BGRA8);
        if (!buffer)
            return;

        Ref frameView = *localMainFrame->view();
        auto originalLayoutViewportOverrideRect = frameView->layoutViewportOverrideRect();
        frameView->setLayoutViewportOverrideRect(LayoutRect(snapshotRect));

        pdfSnapshotAtSize(*localMainFrame, buffer->context(), snapshotRect, { });

        frameView->setLayoutViewportOverrideRect(originalLayoutViewportOverrideRect);
        reply(buffer->sinkIntoPDFDocument());
        return;
    }

    RetainPtr<CFMutableDataRef> pdfPageData;
    drawPagesToPDFImpl(frameID, printInfo, 0, pageCount, pdfPageData);
    reply(SharedBuffer::create(pdfPageData.get()));

    endPrinting();
}

void WebPage::contentSizeCategoryDidChange(const String& contentSizeCategory)
{
    setContentSizeCategory(contentSizeCategory);
    FontCache::invalidateAllFontCaches();
}

String WebPage::platformUserAgent(const URL&) const
{
    return String();
}

static bool isMousePrimaryPointingDevice()
{
#if PLATFORM(MACCATALYST)
    return true;
#else
    return false;
#endif
}

static bool hasAccessoryMousePointingDevice()
{
    if (isMousePrimaryPointingDevice())
        return true;

#if HAVE(MOUSE_DEVICE_OBSERVATION)
    if (WebProcess::singleton().hasMouseDevice())
        return true;
#endif

    return false;
}

static bool hasAccessoryStylusPointingDevice()
{
#if HAVE(STYLUS_DEVICE_OBSERVATION)
    if (WebProcess::singleton().hasStylusDevice())
        return true;
#endif

    return false;
}

bool WebPage::hoverSupportedByPrimaryPointingDevice() const
{
    return isMousePrimaryPointingDevice();
}

bool WebPage::hoverSupportedByAnyAvailablePointingDevice() const
{
    return hasAccessoryMousePointingDevice();
}

std::optional<PointerCharacteristics> WebPage::pointerCharacteristicsOfPrimaryPointingDevice() const
{
    return isMousePrimaryPointingDevice() ? PointerCharacteristics::Fine : PointerCharacteristics::Coarse;
}

OptionSet<PointerCharacteristics> WebPage::pointerCharacteristicsOfAllAvailablePointingDevices() const
{
    OptionSet<PointerCharacteristics> result;
    if (auto pointerCharacteristicsOfPrimaryPointingDevice = this->pointerCharacteristicsOfPrimaryPointingDevice())
        result.add(*pointerCharacteristicsOfPrimaryPointingDevice);
    if (hasAccessoryMousePointingDevice() || hasAccessoryStylusPointingDevice())
        result.add(PointerCharacteristics::Fine);
    return result;
}

void WebPage::hardwareKeyboardAvailabilityChanged(HardwareKeyboardState state)
{
    m_keyboardIsAttached = state.isAttached;
    setHardwareKeyboardState(state);

    if (RefPtr focusedFrame = m_page->focusController().focusedLocalFrame())
        focusedFrame->eventHandler().capsLockStateMayHaveChanged();
}

void WebPage::updateStringForFind(const String& findString)
{
    send(Messages::WebPageProxy::UpdateStringForFind(findString));
}

bool WebPage::platformPrefersTextLegibilityBasedZoomScaling() const
{
#if PLATFORM(WATCHOS)
    return true;
#else
    return false;
#endif
}

void WebPage::updateSelectionWithDelta(int64_t locationDelta, int64_t lengthDelta, CompletionHandler<void()>&& completionHandler)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler();

    RefPtr root = frame->selection().rootEditableElementOrDocumentElement();
    auto selectionRange = frame->selection().selection().toNormalizedRange();
    if (!root || !selectionRange) {
        completionHandler();
        return;
    }

    auto scope = makeRangeSelectingNodeContents(*root);
    auto selectionCharacterRange = characterRange(scope, *selectionRange);
    CheckedInt64 newSelectionLocation { selectionCharacterRange.location };
    CheckedInt64 newSelectionLength { selectionCharacterRange.length };
    newSelectionLocation += locationDelta;
    newSelectionLength += lengthDelta;
    if (newSelectionLocation.hasOverflowed() || newSelectionLength.hasOverflowed()) {
        completionHandler();
        return;
    }

    auto newSelectionRange = CharacterRange(newSelectionLocation, newSelectionLength);
    auto updatedSelectionRange = resolveCharacterRange(makeRangeSelectingNodeContents(*root), newSelectionRange);
    frame->selection().setSelectedRange(updatedSelectionRange, Affinity::Downstream, WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
    completionHandler();
}

static VisiblePosition moveByGranularityRespectingWordBoundary(const VisiblePosition& position, TextGranularity granularity, uint64_t granularityCount, SelectionDirection direction)
{
    ASSERT(granularityCount);
    ASSERT(position.isNotNull());
    bool backwards = direction == SelectionDirection::Backward;
    auto farthestPositionInDirection = backwards ? startOfEditableContent(position) : endOfEditableContent(position);
    if (position == farthestPositionInDirection)
        return backwards ? startOfWord(position) : endOfWord(position);
    VisiblePosition currentPosition = position;
    VisiblePosition nextPosition;
    do {
        nextPosition = positionOfNextBoundaryOfGranularity(currentPosition, granularity, direction);
        if (nextPosition.isNull())
            break;
        currentPosition = nextPosition;
        if (atBoundaryOfGranularity(currentPosition, granularity, direction))
            --granularityCount;
    } while (granularityCount);
    if (granularity == TextGranularity::SentenceGranularity)
        return currentPosition;
    // Note that this rounds to the nearest word, which may cross a line boundary when using line granularity.
    // For example, suppose the text is laid out as follows and the insertion point is at |:
    //     |This is the first sen
    //      tence in a paragraph.
    // Then moving 1 line of granularity forward will return the postion after the 'e' in sentence.
    return backwards ? startOfWord(currentPosition) : endOfWord(currentPosition);
}

static VisiblePosition visiblePositionForPointInRootViewCoordinates(LocalFrame& frame, FloatPoint pointInRootViewCoordinates)
{
    auto pointInDocument = frame.view()->rootViewToContents(roundedIntPoint(pointInRootViewCoordinates));
    return frame.visiblePositionForPoint(pointInDocument);
}

static VisiblePositionRange constrainRangeToSelection(const VisiblePositionRange& selection, const VisiblePositionRange& range)
{
    if (intersects(selection, range))
        return intersection(selection, range);
    auto rangeMidpoint = midpoint(range);
    auto position = startOfWord(rangeMidpoint);
    if (!contains(range, position)) {
        position = endOfWord(rangeMidpoint);
        if (!contains(range, position))
            position = rangeMidpoint;
    }
    return { position, position };
}

void WebPage::requestDocumentEditingContext(DocumentEditingContextRequest&& request, CompletionHandler<void(DocumentEditingContext&&)>&& completionHandler)
{
    bool wantsAttributedText = request.options.contains(DocumentEditingContextRequest::Options::AttributedText);
    if (!request.options.contains(DocumentEditingContextRequest::Options::Text) && !wantsAttributedText) {
        completionHandler({ });
        return;
    }

    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return completionHandler({ });

#if ENABLE(PDF_PLUGIN)
    if (RefPtr pluginView = focusedPluginViewForFrame(*frame))
        return completionHandler(pluginView->documentEditingContext(WTFMove(request)));
#endif

    RefPtr view = frame->view();
    if (!view)
        return completionHandler({ });

    frame->protectedDocument()->updateLayout(LayoutOptions::IgnorePendingStylesheets);

    VisibleSelection selection = frame->selection().selection();

    VisiblePositionRange rangeOfInterest;
    auto selectionRange = VisiblePositionRange { selection.visibleStart(), selection.visibleEnd() };

    bool isSpatialRequest = request.options.containsAny({ DocumentEditingContextRequest::Options::Spatial, DocumentEditingContextRequest::Options::SpatialAndCurrentSelection });
    bool isSpatialRequestWithCurrentSelection = request.options.contains(DocumentEditingContextRequest::Options::SpatialAndCurrentSelection);
    bool wantsRects = request.options.contains(DocumentEditingContextRequest::Options::Rects);
    bool wantsMarkedTextRects = request.options.contains(DocumentEditingContextRequest::Options::MarkedTextRects);

    if (auto textInputContext = request.textInputContext) {
        auto element = elementForContext(*textInputContext);
        if (!element) {
            completionHandler({ });
            return;
        }

        if (!request.rect.isEmpty()) {
            rangeOfInterest.start = closestEditablePositionInElementForAbsolutePoint(*element, roundedIntPoint(request.rect.minXMinYCorner()));
            rangeOfInterest.end = closestEditablePositionInElementForAbsolutePoint(*element, roundedIntPoint(request.rect.maxXMaxYCorner()));
        } else if (RefPtr textFormControlElement = dynamicDowncast<HTMLTextFormControlElement>(element)) {
            rangeOfInterest.start = textFormControlElement->visiblePositionForIndex(0);
            rangeOfInterest.end = textFormControlElement->visiblePositionForIndex(textFormControlElement->value()->length());
        } else {
            rangeOfInterest.start = firstPositionInOrBeforeNode(element.get());
            rangeOfInterest.end = lastPositionInOrAfterNode(element.get());
        }
    } else if (isSpatialRequest) {
        // FIXME: We might need to be a bit more careful that we get something useful (test the other corners?).
        rangeOfInterest.start = visiblePositionForPointInRootViewCoordinates(*frame, request.rect.minXMinYCorner());
        rangeOfInterest.end = visiblePositionForPointInRootViewCoordinates(*frame, request.rect.maxXMaxYCorner());

        if (isSpatialRequestWithCurrentSelection && !selection.isNoneOrOrphaned()) {
            static constexpr auto maximumNumberOfLines = 10;
            auto intersectsSpatialRect = [&](const VisiblePosition& position) {
                FloatRect caretInRootView = view->contentsToRootView(position.absoluteCaretBounds());
                return caretInRootView.intersects(request.rect);
            };

            auto startPositionNearSelection = [&] {
                auto start = selectionRange.start;
                unsigned lineCount = 0;
                do {
                    auto previous = previousLinePosition(start, start.lineDirectionPointForBlockDirectionNavigation());
                    if (previous.isNull() || previous == start)
                        break;
                    start = WTFMove(previous);
                    lineCount++;
                } while (intersectsSpatialRect(start) && lineCount < maximumNumberOfLines);
                return start;
            }();

            auto endPositionNearSelection = [&] {
                auto end = selectionRange.end;
                unsigned lineCount = 0;
                do {
                    auto next = nextLinePosition(end, end.lineDirectionPointForBlockDirectionNavigation());
                    if (next.isNull() || next == end)
                        break;
                    end = WTFMove(next);
                    lineCount++;
                } while (intersectsSpatialRect(end) && lineCount < maximumNumberOfLines);
                return end;
            }();

            if (startPositionNearSelection <= endPositionNearSelection) {
                rangeOfInterest.start = std::clamp(rangeOfInterest.start, startPositionNearSelection, endPositionNearSelection);
                rangeOfInterest.end = std::clamp(rangeOfInterest.end, startPositionNearSelection, endPositionNearSelection);
            }
        }
        if (request.options.contains(DocumentEditingContextRequest::Options::SpatialAndCurrentSelection)) {
            if (RefPtr rootEditableElement = selection.rootEditableElement()) {
                VisiblePosition startOfEditableRoot { firstPositionInOrBeforeNode(rootEditableElement.get()) };
                VisiblePosition endOfEditableRoot { lastPositionInOrAfterNode(rootEditableElement.get()) };
                if (startOfEditableRoot <= endOfEditableRoot) {
                    rangeOfInterest.start = std::clamp(rangeOfInterest.start, startOfEditableRoot, endOfEditableRoot);
                    rangeOfInterest.end = std::clamp(rangeOfInterest.end, startOfEditableRoot, endOfEditableRoot);
                }
            }
        }
    } else if (!selection.isNone())
        rangeOfInterest = selectionRange;

    if (rangeOfInterest.end < rangeOfInterest.start)
        std::exchange(rangeOfInterest.start, rangeOfInterest.end);

    if (request.options.contains(DocumentEditingContextRequest::Options::SpatialAndCurrentSelection)) {
        if (selectionRange.start < rangeOfInterest.start)
            rangeOfInterest.start = selectionRange.start;
        if (selectionRange.end > rangeOfInterest.end)
            rangeOfInterest.end = selectionRange.end;
    }

    if (rangeOfInterest.start.isNull() || rangeOfInterest.start.isOrphan() || rangeOfInterest.end.isNull() || rangeOfInterest.end.isOrphan()) {
        completionHandler({ });
        return;
    }

    // The subset of the selection that is inside the range of interest.
    auto rangeOfInterestInSelection = constrainRangeToSelection(selection, rangeOfInterest);
    if (rangeOfInterestInSelection.isNull()) {
        completionHandler({ });
        return;
    }

    VisiblePosition contextBeforeStart;
    VisiblePosition contextAfterEnd;
    auto compositionRange = frame->editor().compositionRange();
    if (request.granularityCount) {
        contextBeforeStart = moveByGranularityRespectingWordBoundary(rangeOfInterest.start, request.surroundingGranularity, request.granularityCount, SelectionDirection::Backward);
        contextAfterEnd = moveByGranularityRespectingWordBoundary(rangeOfInterest.end, request.surroundingGranularity, request.granularityCount, SelectionDirection::Forward);
    } else {
        contextBeforeStart = rangeOfInterest.start;
        contextAfterEnd = rangeOfInterest.end;
        if (wantsMarkedTextRects && compositionRange) {
            // In the case where the client has requested marked text rects make sure that the context
            // range encompasses the entire marked text range so that we don't return a truncated result.
            auto compositionStart = makeDeprecatedLegacyPosition(compositionRange->start);
            auto compositionEnd = makeDeprecatedLegacyPosition(compositionRange->end);
            if (contextBeforeStart > compositionStart)
                contextBeforeStart = compositionStart;
            if (contextAfterEnd < compositionEnd)
                contextAfterEnd = compositionEnd;
        }
    }

    auto isTextObscured = [] (const VisiblePosition& visiblePosition) {
        if (RefPtr textControl = enclosingTextFormControl(visiblePosition.deepEquivalent())) {
            if (RefPtr input = dynamicDowncast<HTMLInputElement>(textControl.get())) {
                if (input->autofilledAndObscured())
                    return true;
            }
        }
        return false;
    };

    auto makeString = [&] (const VisiblePosition& start, const VisiblePosition& end) -> AttributedString {
        auto range = makeSimpleRange(start, end);
        if (!range || range->collapsed())
            return { };

        auto isObscured = isTextObscured(start);
        TextIteratorBehaviors textBehaviors = { };
        if (!isObscured)
            textBehaviors = TextIteratorBehavior::EmitsOriginalText;

        if (wantsAttributedText)
            return editingAttributedStringReplacingNoBreakSpace(*range, textBehaviors, { IncludedElement::Images, IncludedElement::Attachments });

        return AttributedString::fromNSAttributedString(adoptNS([[NSAttributedString alloc] initWithString:plainTextReplacingNoBreakSpace(*range, textBehaviors).createNSString().get()]));
    };

    DocumentEditingContext context;
    context.contextBefore = makeString(contextBeforeStart, rangeOfInterestInSelection.start);
    context.selectedText = makeString(rangeOfInterestInSelection.start, rangeOfInterestInSelection.end);
    context.contextAfter = makeString(rangeOfInterestInSelection.end, contextAfterEnd);
    if (auto compositionVisiblePositionRange = makeVisiblePositionRange(compositionRange); intersects(rangeOfInterest, compositionVisiblePositionRange)) {
        context.markedText = makeString(compositionVisiblePositionRange.start, compositionVisiblePositionRange.end);
        auto markedTextLength = context.markedText.string.length();

        ptrdiff_t distanceToSelectionStart = distanceBetweenPositions(rangeOfInterestInSelection.start, compositionVisiblePositionRange.start);
        ptrdiff_t distanceToSelectionEnd = distanceToSelectionStart + [context.selectedText.string.createNSString() length];

        distanceToSelectionStart = clampTo<ptrdiff_t>(distanceToSelectionStart, 0, markedTextLength);
        distanceToSelectionEnd = clampTo<ptrdiff_t>(distanceToSelectionEnd, 0, markedTextLength);
        RELEASE_ASSERT(distanceToSelectionStart <= distanceToSelectionEnd);

        context.selectedRangeInMarkedText = {
            static_cast<uint64_t>(distanceToSelectionStart),
            static_cast<uint64_t>(distanceToSelectionEnd - distanceToSelectionStart)
        };
    } else if (auto* suggestion = frame->editor().writingSuggestionData()) {
        if (auto suffix = suggestion->content(); !suffix.isEmpty()) {
            context.markedText = AttributedString::fromNSAttributedString(adoptNS([[NSAttributedString alloc] initWithString:suffix.createNSString().get()]));
            context.selectedRangeInMarkedText = { 0, 0 };
        }
    }

    auto characterRectsForRange = [](const SimpleRange& range, unsigned startOffset) {
        Vector<DocumentEditingContext::TextRectAndRange> rects;
        unsigned offsetSoFar = startOffset;
        std::optional<SimpleRange> lastTextRange;
        for (TextIterator iterator { range }; !iterator.atEnd(); iterator.advance()) {
            if (iterator.text().isEmpty())
                continue;

            if (lastTextRange == iterator.range())
                continue;

            Vector<IntRect> absoluteRects;
            if (iterator.range().collapsed())
                absoluteRects = { VisiblePosition(makeContainerOffsetPosition(iterator.range().start)).absoluteCaretBounds() };
            else {
                absoluteRects = RenderObject::absoluteTextRects(iterator.range(), {
                    RenderObject::BoundingRectBehavior::IgnoreEmptyTextSelections,
                    RenderObject::BoundingRectBehavior::ComputeIndividualCharacterRects,
                });
                if (absoluteRects.isEmpty())
                    absoluteRects.append({ });
            }

            for (auto& absoluteRect : absoluteRects)
                rects.append({ iterator.range().start.document().view()->contentsToRootView(absoluteRect), { offsetSoFar++, 1 } });

            lastTextRange = iterator.range();
        }
        return rects;
    };

    if (wantsRects) {
        if (auto contextRange = makeSimpleRange(contextBeforeStart, contextAfterEnd)) {
            // FIXME (257828): We should ideally ASSERT() here that context.textRects.size() is equal to
            // the combined length of the context and selection strings; however, there are some corner
            // cases in which this isn't true. In particular, such an assertion would be hit when running
            // the layout test editing/selection/ios/update-selection-after-overflow-scroll.html.
            // See also: <https://bugs.webkit.org/show_bug.cgi?id=257828>.
            context.textRects = characterRectsForRange(*contextRange, 0);
        }
    } else if (wantsMarkedTextRects && compositionRange) {
        unsigned compositionStartOffset = 0;
        if (auto range = makeSimpleRange(contextBeforeStart, compositionRange->start))
            compositionStartOffset = characterCount(*range);
        context.textRects = characterRectsForRange(*compositionRange, compositionStartOffset);
    }

#if ENABLE(PLATFORM_DRIVEN_TEXT_CHECKING)
    if (request.options.contains(DocumentEditingContextRequest::Options::Annotation))
        context.annotatedText = m_textCheckingControllerProxy->annotatedSubstringBetweenPositions(contextBeforeStart, contextAfterEnd);
#endif

    if (request.options.contains(DocumentEditingContextRequest::Options::AutocorrectedRanges)) {
        if (auto contextRange = makeSimpleRange(contextBeforeStart, contextAfterEnd)) {
            auto ranges = frame->document()->markers().rangesForMarkersInRange(*contextRange, DocumentMarkerType::CorrectionIndicator);
            context.autocorrectedRanges = ranges.map([&] (auto& range) {
                auto characterRangeInContext = characterRange(*contextRange, range);
                return DocumentEditingContext::Range { characterRangeInContext.location, characterRangeInContext.length };
            });
        }
    }

    completionHandler(WTFMove(context));
}

bool WebPage::shouldAllowSingleClickToChangeSelection(WebCore::Node& targetNode, const WebCore::VisibleSelection& newSelection)
{
    if (RefPtr editableRoot = newSelection.rootEditableElement(); editableRoot && editableRoot == targetNode.rootEditableElement()) {
        // Text interaction gestures will handle selection in the case where we are already editing the node. In the case where we're
        // just starting to focus an editable element by tapping on it, only change the selection if we weren't already showing an
        // input view prior to handling the tap.
        return !(m_completingSyntheticClick ? m_wasShowingInputViewForFocusedElementDuringLastPotentialTap : m_isShowingInputViewForFocusedElement);
    }
    return true;
}

void WebPage::setShouldRevealCurrentSelectionAfterInsertion(bool shouldRevealCurrentSelectionAfterInsertion)
{
    if (m_shouldRevealCurrentSelectionAfterInsertion == shouldRevealCurrentSelectionAfterInsertion)
        return;
    m_shouldRevealCurrentSelectionAfterInsertion = shouldRevealCurrentSelectionAfterInsertion;
    if (!shouldRevealCurrentSelectionAfterInsertion)
        return;
    m_page->revealCurrentSelection();
    scheduleFullEditorStateUpdate();
}

void WebPage::setScreenIsBeingCaptured(bool captured)
{
    m_screenIsBeingCaptured = captured;
}

void WebPage::setInsertionPointColor(WebCore::Color color)
{
    RenderThemeIOS::setInsertionPointColor(color);
}

void WebPage::textInputContextsInRect(FloatRect searchRect, CompletionHandler<void(const Vector<ElementContext>&)>&& completionHandler)
{
    auto contexts = m_page->editableElementsInRect(searchRect).map([&] (const auto& element) {
        auto& document = element->document();

        ElementContext context;
        context.webPageIdentifier = m_identifier;
        context.documentIdentifier = document.identifier();
        context.nodeIdentifier = element->nodeIdentifier();
        context.boundingRect = element->boundingBoxInRootViewCoordinates();
        return context;
    });
    completionHandler(contexts);
#if ENABLE(EDITABLE_REGION)
    m_page->setEditableRegionEnabled();
#endif
}

void WebPage::focusTextInputContextAndPlaceCaret(const ElementContext& elementContext, const IntPoint& point, CompletionHandler<void(bool)>&& completionHandler)
{
    RefPtr target = elementForContext(elementContext);
    if (!target) {
        completionHandler(false);
        return;
    }

    ASSERT(target->document().frame());
    Ref targetFrame = *target->document().frame();

    targetFrame->protectedDocument()->updateLayout(LayoutOptions::IgnorePendingStylesheets);

    // Performing layout could have could torn down the element's renderer. Check that we still
    // have one. Otherwise, bail out as this function only focuses elements that have a visual
    // representation.
    if (!target->renderer() || !target->isFocusable()) {
        completionHandler(false);
        return;
    }

    // FIXME: Do not focus an element if it moved or the caret point is outside its bounds
    // because we only want to do so if the caret can be placed.
    UserGestureIndicator gestureIndicator { IsProcessingUserGesture::Yes, &target->document() };
    SetForScope userIsInteractingChange { m_userIsInteracting, true };
    protectedCorePage()->focusController().setFocusedElement(target.get(), targetFrame);

    // Setting the focused element could tear down the element's renderer. Check that we still have one.
    if (m_focusedElement != target || !target->renderer()) {
        completionHandler(false);
        return;
    }

    ASSERT(targetFrame->view());
    auto position = closestEditablePositionInElementForAbsolutePoint(*target, targetFrame->view()->rootViewToContents(point));
    if (position.isNull()) {
        completionHandler(false);
        return;
    }
    targetFrame->selection().setSelectedRange(makeSimpleRange(position), position.affinity(), WebCore::FrameSelection::ShouldCloseTyping::Yes, UserTriggered::Yes);
    completionHandler(true);
}

void WebPage::platformDidScalePage()
{
    auto transactionID = downcast<RemoteLayerTreeDrawingArea>(*m_drawingArea).lastCommittedTransactionID();
    m_internals->lastLayerTreeTransactionIdAndPageScaleBeforeScalingPage = { { transactionID, m_lastTransactionPageScaleFactor } };
}

#if USE(QUICK_LOOK)

void WebPage::didStartLoadForQuickLookDocumentInMainFrame(const String& fileName, const String& uti)
{
    send(Messages::WebPageProxy::DidStartLoadForQuickLookDocumentInMainFrame(fileName, uti));
}

void WebPage::didFinishLoadForQuickLookDocumentInMainFrame(const FragmentedSharedBuffer& buffer)
{
    ASSERT(!buffer.isEmpty());

    // FIXME: In some cases, buffer contains a single segment that wraps an existing ShareableResource.
    // If we could create a handle from that existing resource then we could avoid this extra
    // allocation and copy.

    auto sharedMemory = SharedMemory::copyBuffer(buffer);
    if (!sharedMemory)
        return;

    auto shareableResource = ShareableResource::create(sharedMemory.releaseNonNull(), 0, buffer.size());
    if (!shareableResource)
        return;

    auto handle = shareableResource->createHandle();
    if (!handle)
        return;

    send(Messages::WebPageProxy::DidFinishLoadForQuickLookDocumentInMainFrame(WTFMove(*handle)));
}

void WebPage::requestPasswordForQuickLookDocumentInMainFrame(const String& fileName, CompletionHandler<void(const String&)>&& completionHandler)
{
    sendWithAsyncReply(Messages::WebPageProxy::RequestPasswordForQuickLookDocumentInMainFrame(fileName), WTFMove(completionHandler));
}

#endif

void WebPage::animationDidFinishForElement(const Element& animatedElement)
{
    scheduleEditorStateUpdateAfterAnimationIfNeeded(animatedElement);

    if (!m_page->settings().layoutViewportHeightExpansionFactor())
        return;

    if (!animatedElement.document().isTopDocument())
        return;

    if (CheckedPtr renderer = animatedElement.renderer(); renderer && renderer->isFixedPositioned())
        scheduleLayoutViewportHeightExpansionUpdate();
}

void WebPage::scheduleEditorStateUpdateAfterAnimationIfNeeded(const Element& animatedElement)
{
    RefPtr frame = m_page->focusController().focusedOrMainFrame();
    if (!frame)
        return;

    auto& selection = frame->selection().selection();
    if (selection.isNoneOrOrphaned())
        return;

    if (selection.isCaret() && !selection.hasEditableStyle())
        return;

    auto scheduleEditorStateUpdateForStartOrEndContainerNodeIfNeeded = [&](const Node* container) {
        if (!animatedElement.isShadowIncludingInclusiveAncestorOf(container))
            return false;

        frame->selection().setCaretRectNeedsUpdate();
        scheduleFullEditorStateUpdate();
        return true;
    };

    RefPtr startContainer = selection.start().containerNode();
    if (scheduleEditorStateUpdateForStartOrEndContainerNodeIfNeeded(startContainer.get()))
        return;

    RefPtr endContainer = selection.end().containerNode();
    if (startContainer != endContainer)
        scheduleEditorStateUpdateForStartOrEndContainerNodeIfNeeded(endContainer.get());
}

FloatSize WebPage::screenSizeForFingerprintingProtections(const LocalFrame&, FloatSize defaultSize) const
{
    if (!PAL::currentUserInterfaceIdiomIsSmallScreen())
        return m_viewportConfiguration.minimumLayoutSize();

    static constexpr std::array fixedSizes {
        FloatSize { 320, 568 },
        FloatSize { 375, 667 },
        FloatSize { 390, 844 },
        FloatSize { 414, 896 },
    };

    for (auto fixedSize : fixedSizes) {
        if (defaultSize.width() <= fixedSize.width())
            return fixedSize;
    }

    return std::get<std::tuple_size_v<decltype(fixedSizes)> - 1>(fixedSizes);
}

void WebPage::shouldDismissKeyboardAfterTapAtPoint(FloatPoint point, CompletionHandler<void(bool)>&& completion)
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return completion(false);

    RefPtr mainFrameView = localMainFrame->view();
    if (!mainFrameView)
        return completion(false);

    FloatPoint adjustedPoint;
    RefPtr target = localMainFrame->nodeRespondingToClickEvents(point, adjustedPoint);
    if (!target)
        return completion(true);

    if (target->hasEditableStyle())
        return completion(false);

    if (RefPtr element = dynamicDowncast<Element>(*target); element && element->isFormControlElement())
        return completion(false);

    auto minimumSizeForDismissal = FloatSize { 0.9f * mainFrameView->unobscuredContentSize().width(), 150.f };

    bool isReplaced;
    FloatSize targetSize = target->absoluteBoundingRect(&isReplaced).size();
    completion(targetSize.width() >= minimumSizeForDismissal.width() && targetSize.height() >= minimumSizeForDismissal.height());
}

void WebPage::computeEnclosingLayerID(EditorState& state, const VisibleSelection& selection) const
{
    auto selectionRange = selection.range();

    if (!selectionRange)
        return;

    auto [startLayer, endLayer, enclosingLayer, graphicsLayer, enclosingGraphicsLayerID] = computeEnclosingLayer(*selectionRange);

    state.visualData->enclosingLayerID = WTFMove(enclosingGraphicsLayerID);

    if (!state.visualData->enclosingLayerID)
        return;

    auto nextScroller = [](RenderLayer& layer, IncludeSelfOrNot includeSelf) {
        return layer.enclosingScrollableLayer(includeSelf, CrossFrameBoundaries::Yes);
    };

    auto scrollOffsetAndNodeIDForLayer = [](RenderLayer* layer) -> std::pair<ScrollOffset, std::optional<ScrollingNodeID>> {
        CheckedRef renderer = layer->renderer();
        WeakPtr scrollableArea = [&] -> ScrollableArea* {
            if (renderer->isRenderView())
                return renderer->document().isTopDocument() ? nullptr : renderer->frame().view();

            return layer->scrollableArea();
        }();

        if (!scrollableArea)
            return { };

        auto scrollingNodeID = scrollableArea->scrollingNodeID();
        if (!scrollingNodeID)
            return { };

        return { scrollableArea->scrollOffset(), WTFMove(scrollingNodeID) };
    };

    CheckedPtr<RenderLayer> scrollableLayer;
    for (CheckedPtr layer = nextScroller(*enclosingLayer, IncludeSelfOrNot::IncludeSelf); layer; layer = nextScroller(*layer, IncludeSelfOrNot::ExcludeSelf)) {
        if (auto [scrollOffset, scrollingNodeID] = scrollOffsetAndNodeIDForLayer(layer.get()); scrollingNodeID) {
            state.visualData->enclosingScrollOffset = scrollOffset;
            state.visualData->enclosingScrollingNodeID = WTFMove(scrollingNodeID);
            scrollableLayer = WTFMove(layer);
            break;
        }
    }

    ASSERT_IMPLIES(state.visualData->enclosingLayerID, graphicsLayer);
    state.visualData->enclosingLayerUsesContentsLayer = graphicsLayer && graphicsLayer->usesContentsLayer();

    if (selection.isCaret()) {
        state.visualData->scrollingNodeIDAtStart = state.visualData->enclosingScrollingNodeID;
        state.visualData->scrollingNodeIDAtEnd = state.visualData->enclosingScrollingNodeID;
        return;
    }

    auto scrollingNodeIDForEndpoint = [&](RenderLayer* endpointLayer) {
        for (CheckedPtr layer = endpointLayer; layer && layer != scrollableLayer; layer = nextScroller(*layer, IncludeSelfOrNot::ExcludeSelf)) {
            if (auto scrollingNodeID = scrollOffsetAndNodeIDForLayer(layer.get()).second)
                return scrollingNodeID;
        }
        return state.visualData->enclosingScrollingNodeID;
    };

    state.visualData->scrollingNodeIDAtStart = scrollingNodeIDForEndpoint(startLayer.get());
    state.visualData->scrollingNodeIDAtEnd = scrollingNodeIDForEndpoint(endLayer.get());
}

void WebPage::invokePendingSyntheticClickCallback(SyntheticClickResult result)
{
    if (auto callback = std::exchange(m_pendingSyntheticClickCallback, { }))
        callback(result);
}

void WebPage::callAfterPendingSyntheticClick(CompletionHandler<void(SyntheticClickResult)>&& completion)
{
    if (m_pendingSyntheticClickCallback)
        return completion(SyntheticClickResult::Failed);

    sendWithAsyncReply(Messages::WebPageProxy::IsPotentialTapInProgress(), [weakPage = WeakPtr { *this }, completion = WTFMove(completion)](bool isTapping) mutable {
        RefPtr page = weakPage.get();
        if (!page || page->m_isClosed)
            return completion(SyntheticClickResult::PageInvalid);

        if (!isTapping)
            return completion(SyntheticClickResult::Failed);

        if (!page->m_potentialTapNode)
            return completion(SyntheticClickResult::Failed);

        page->m_pendingSyntheticClickCallback = WTFMove(completion);
    });
}

#if ENABLE(IOS_TOUCH_EVENTS)

void WebPage::didDispatchClickEvent(const PlatformMouseEvent& event, Node& node)
{
    if (!m_userIsInteracting)
        return;

    if (event.type() != PlatformEventType::MouseReleased)
        return;

    if (event.syntheticClickType() != SyntheticClickType::NoTap)
        return;

    RefPtr element = dynamicDowncast<Element>(node) ?: node.parentElementInComposedTree();
    if (!element)
        return;

    Ref document = node.document();
    if (!document->quirks().shouldSynthesizeTouchEventsAfterNonSyntheticClick(*element))
        return;

    bool isReplaced = false;
    auto bounds = element->absoluteBoundingRect(&isReplaced);
    if (bounds.isEmpty())
        return;

    callOnMainRunLoop([bounds, document = WTFMove(document)] mutable {
        if (RefPtr frame = document->frame())
            frame->eventHandler().dispatchSimulatedTouchEvent(roundedIntPoint(bounds.center()));
    });
}

#endif // ENABLE(IOS_TOUCH_EVENTS)

#if USE(UICONTEXTMENU)

void WebPage::willBeginContextMenuInteraction()
{
    m_hasActiveContextMenuInteraction = true;
}

void WebPage::didEndContextMenuInteraction()
{
    m_hasActiveContextMenuInteraction = false;
}

#endif // USE(UICONTEXTMENU)

#if ENABLE(PDF_PAGE_NUMBER_INDICATOR)

void WebPage::createPDFPageNumberIndicator(PDFPluginBase& plugin, const IntRect& boundingBox, size_t pageCount)
{
    ASSERT(!m_pdfPlugInWithPageNumberIndicator.first || m_pdfPlugInWithPageNumberIndicator.first == plugin.identifier());
    if (m_pdfPlugInWithPageNumberIndicator.first == plugin.identifier())
        return;
    m_pdfPlugInWithPageNumberIndicator = std::make_pair(plugin.identifier(), WeakPtr { plugin });
    send(Messages::WebPageProxy::CreatePDFPageNumberIndicator(plugin.identifier(), boundingBox, pageCount));
}

void WebPage::updatePDFPageNumberIndicatorLocation(PDFPluginBase& plugin, const IntRect& boundingBox)
{
    if (m_pdfPlugInWithPageNumberIndicator.first == plugin.identifier())
        send(Messages::WebPageProxy::UpdatePDFPageNumberIndicatorLocation(plugin.identifier(), boundingBox));
}

void WebPage::updatePDFPageNumberIndicatorCurrentPage(PDFPluginBase& plugin, size_t pageIndex)
{
    if (m_pdfPlugInWithPageNumberIndicator.first == plugin.identifier())
        send(Messages::WebPageProxy::UpdatePDFPageNumberIndicatorCurrentPage(plugin.identifier(), pageIndex));
}

void WebPage::removePDFPageNumberIndicator(PDFPluginBase& plugin)
{
    if (m_pdfPlugInWithPageNumberIndicator.first == plugin.identifier()) {
        m_pdfPlugInWithPageNumberIndicator = std::make_pair(Markable<PDFPluginIdentifier> { }, nullptr);
        send(Messages::WebPageProxy::RemovePDFPageNumberIndicator(plugin.identifier()));
    }
}

#endif

} // namespace WebKit

#undef WEBPAGE_RELEASE_LOG
#undef WEBPAGE_RELEASE_LOG_ERROR

#endif // PLATFORM(IOS_FAMILY)
