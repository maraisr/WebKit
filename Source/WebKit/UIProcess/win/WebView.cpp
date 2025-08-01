/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2017 Sony Interactive Entertainment Inc.
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
#include "WebView.h"

#include "APIPageConfiguration.h"
#include "DrawingAreaProxyCoordinatedGraphics.h"
#include "Logging.h"
#include "NativeWebKeyboardEvent.h"
#include "NativeWebMouseEvent.h"
#include "NativeWebWheelEvent.h"
#include "WKAPICast.h"
#include "WebContextMenuProxyWin.h"
#include "WebEditCommandProxy.h"
#include "WebEventFactory.h"
#include "WebKitDLL.h"
#include "WebPageGroup.h"
#include "WebPageProxy.h"
#include "WebProcessPool.h"
#include <Commctrl.h>
#include <WebCore/Cursor.h>
#include <WebCore/Editor.h>
#include <WebCore/FloatRect.h>
#include <WebCore/GDIUtilities.h>
#include <WebCore/HWndDC.h>
#include <WebCore/IntRect.h>
#include <WebCore/NotImplemented.h>
#include <WebCore/Region.h>
#include <WebCore/WindowMessageBroadcaster.h>
#include <wtf/FileSystem.h>
#include <wtf/SoftLinking.h>
#include <wtf/text/StringBuffer.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/WTFString.h>

#if ENABLE(REMOTE_INSPECTOR)
#include "RemoteInspectorProtocolHandler.h"
#endif

#if USE(CAIRO)
#include <WebCore/CairoUtilities.h>
#include <cairo-win32.h>
#include <cairo.h>
#endif

#if USE(SKIA)
#include <WebCore/BitmapInfo.h>
#include <skia/core/SkCanvas.h>
#endif

#if USE(GRAPHICS_LAYER_WC)
#include "DrawingAreaProxyWC.h"
#endif

namespace WebKit {
using namespace WebCore;

static const LPCWSTR kWebKit2WebViewWindowClassName = L"WebKit2WebViewWindowClass";

static const int kMaxToolTipWidth = 250;

enum {
    UpdateActiveStateTimer = 1,
};

LRESULT CALLBACK WebView::WebViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LONG_PTR longPtr = ::GetWindowLongPtr(hWnd, 0);

    if (WebView* webView = reinterpret_cast<WebView*>(longPtr))
        return webView->wndProc(hWnd, message, wParam, lParam);

    if (message == WM_CREATE) {
        LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);

        // Associate the WebView with the window.
        ::SetWindowLongPtr(hWnd, 0, (LONG_PTR)createStruct->lpCreateParams);
        return 0;
    }

    return ::DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT WebView::wndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT lResult = 0;
    bool handled = true;

    switch (message) {
    case WM_CLOSE:
        m_page->tryClose();
        break;
    case WM_DESTROY:
        m_isBeingDestroyed = true;
        closeInternal();
        break;
    case WM_ERASEBKGND:
        lResult = 1;
        break;
    case WM_PAINT:
        lResult = onPaintEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_PRINTCLIENT:
        lResult = onPrintClientEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_MOUSEACTIVATE:
        setWasActivatedByMouseEvent(true);
        handled = false;
        break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MOUSELEAVE:
        lResult = onMouseEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_MOUSEWHEEL:
        lResult = onWheelEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_HSCROLL:
        lResult = onHorizontalScroll(hWnd, message, wParam, lParam, handled);
        break;
    case WM_VSCROLL:
        lResult = onVerticalScroll(hWnd, message, wParam, lParam, handled);
        break;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    case WM_SYSCHAR:
    case WM_CHAR:
    case WM_SYSKEYUP:
    case WM_KEYUP:
        lResult = onKeyEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_SIZE:
        lResult = onSizeEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_WINDOWPOSCHANGED:
        lResult = onWindowPositionChangedEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_SETFOCUS:
        lResult = onSetFocusEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_KILLFOCUS:
        lResult = onKillFocusEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_TIMER:
        lResult = onTimerEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_SHOWWINDOW:
        lResult = onShowWindowEvent(hWnd, message, wParam, lParam, handled);
        break;
    case WM_SETCURSOR:
        lResult = onSetCursor(hWnd, message, wParam, lParam, handled);
        break;
    case WM_MENUCOMMAND:
        lResult = onMenuCommand(hWnd, message, wParam, lParam, handled);
        break;
    case WM_COMMAND:
        SendMessage(GetParent(hWnd), message, wParam, lParam);
        break;
    default:
        handled = false;
        break;
    }

    if (!handled)
        lResult = ::DefWindowProc(hWnd, message, wParam, lParam);

    return lResult;
}

bool WebView::registerWebViewWindowClass()
{
    static bool haveRegisteredWindowClass = false;
    if (haveRegisteredWindowClass)
        return true;
    haveRegisteredWindowClass = true;

    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = WebView::WebViewWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(WebView*);
    wcex.hInstance = instanceHandle();
    wcex.hIcon = 0;
    wcex.hCursor = ::LoadCursor(0, IDC_ARROW);
    wcex.hbrBackground = 0;
    wcex.lpszMenuName = 0;
    wcex.lpszClassName = kWebKit2WebViewWindowClassName;
    wcex.hIconSm = 0;

    return !!::RegisterClassEx(&wcex);
}

WebView::WebView(RECT rect, const API::PageConfiguration& configuration, HWND parentWindow)
    : m_pageClient(makeUniqueWithoutRefCountedCheck<PageClientImpl>(*this))
{
    registerWebViewWindowClass();

    m_window = ::CreateWindowExW(0, kWebKit2WebViewWindowClassName, 0, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, parentWindow ? parentWindow : HWND_MESSAGE, 0, instanceHandle(), this);
    ASSERT(::IsWindow(m_window));
    // We only check our window style, and not ::IsWindowVisible, because m_isVisible only tracks
    // this window's visibility status, while ::IsWindowVisible takes our ancestors' visibility
    // status into account. <http://webkit.org/b/54104>
    ASSERT(m_isVisible == static_cast<bool>(::GetWindowLong(m_window, GWL_STYLE) & WS_VISIBLE));

    Ref pageConfiguration = configuration.copy();
    pageConfiguration->preferences().setAllowTestOnlyIPC(pageConfiguration->allowTestOnlyIPC());
    WebProcessPool& processPool = pageConfiguration->processPool();
    m_page = processPool.createWebPage(*m_pageClient, WTFMove(pageConfiguration));

    auto& configurationFromPage = m_page->configuration();
    m_page->initializeWebPage(configurationFromPage.openedSite(), configurationFromPage.initialSandboxFlags());

    m_page->setIntrinsicDeviceScaleFactor(deviceScaleFactorForWindow(m_window));

#if ENABLE(REMOTE_INSPECTOR)
    m_page->setURLSchemeHandlerForScheme(RemoteInspectorProtocolHandler::create(*m_page), "inspector"_s);
#endif

    // FIXME: Initializing the tooltip window here matches WebKit win, but seems like something
    // we could do on demand to save resources.
    initializeToolTipWindow();

    // Initialize the top level parent window and register it with the WindowMessageBroadcaster.
    windowAncestryDidChange();
}

WebView::~WebView()
{
    // Tooltip window needs to be explicitly destroyed since it isn't a WS_CHILD.
    if (::IsWindow(m_toolTipWindow))
        ::DestroyWindow(m_toolTipWindow);
}

void WebView::initialize()
{
    if (shouldInitializeTrackPointHack()) {
        // If we detected a registry key belonging to a TrackPoint driver, then create fake
        // scrollbars, so the WebView will receive WM_VSCROLL and WM_HSCROLL messages.
        // We create an invisible vertical scrollbar and an invisible horizontal scrollbar to allow
        // for receiving both types of messages.
        ::CreateWindow(TEXT("SCROLLBAR"), TEXT("FAKETRACKPOINTHSCROLLBAR"), WS_CHILD | WS_VISIBLE | SBS_HORZ, 0, 0, 0, 0, m_window, 0, instanceHandle(), 0);
        ::CreateWindow(TEXT("SCROLLBAR"), TEXT("FAKETRACKPOINTVSCROLLBAR"), WS_CHILD | WS_VISIBLE | SBS_VERT, 0, 0, 0, 0, m_window, 0, instanceHandle(), 0);
    }
}

void WebView::setParentWindow(HWND parentWindow)
{
    if (m_window) {
        // If the host window hasn't changed, bail.
        if (::GetParent(m_window) == parentWindow)
            return;
        if (parentWindow)
            ::SetParent(m_window, parentWindow);
        else if (!m_isBeingDestroyed) {
            // Turn the WebView into a message-only window so it will no longer be a child of the
            // old parent window and will be hidden from screen. We only do this when
            // isBeingDestroyed() is false because doing this while handling WM_DESTROY can leave
            // m_window in a weird state (see <http://webkit.org/b/29337>).
            ::SetParent(m_window, HWND_MESSAGE);
        }
    }

    windowAncestryDidChange();
}

static HWND findTopLevelParentWindow(HWND window)
{
    if (!window)
        return 0;

    HWND current = window;
    for (HWND parent = GetParent(current); current; current = parent, parent = GetParent(parent)) {
        if (!parent || !(GetWindowLongPtr(current, GWL_STYLE) & (WS_POPUP | WS_CHILD)))
            return current;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

void WebView::windowAncestryDidChange()
{
    HWND newTopLevelParentWindow;
    if (m_window)
        newTopLevelParentWindow = findTopLevelParentWindow(m_window);
    else {
        // There's no point in tracking active state changes of our parent window if we don't have
        // a window ourselves.
        newTopLevelParentWindow = 0;
    }

    if (newTopLevelParentWindow == m_topLevelParentWindow)
        return;

    if (m_topLevelParentWindow)
        WindowMessageBroadcaster::removeListener(m_topLevelParentWindow, this);

    m_topLevelParentWindow = newTopLevelParentWindow;

    if (m_topLevelParentWindow)
        WindowMessageBroadcaster::addListener(m_topLevelParentWindow, this);

    updateActiveState();
}

LRESULT WebView::onMouseEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    NativeWebMouseEvent mouseEvent = NativeWebMouseEvent(hWnd, message, wParam, lParam, m_wasActivatedByMouseEvent, m_page->intrinsicDeviceScaleFactor());
    setWasActivatedByMouseEvent(false);

    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        ::SetFocus(m_window);
        ::SetCapture(m_window);
        break;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        ::ReleaseCapture();
        break;
    case WM_MOUSEMOVE:
        startTrackingMouseLeave();
        break;
    case WM_MOUSELEAVE:
        stopTrackingMouseLeave();
        break;
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    m_page->handleMouseEvent(mouseEvent);

    handled = true;
    return 0;
}

LRESULT WebView::onWheelEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    NativeWebWheelEvent wheelEvent(hWnd, message, wParam, lParam, m_page->intrinsicDeviceScaleFactor());
    if (wheelEvent.controlKey()) {
        // We do not want WebKit to handle Control + Wheel, this should be handled by the client application
        // to zoom the page.
        handled = false;
        return 0;
    }

    m_page->handleNativeWheelEvent(wheelEvent);

    handled = true;
    return 0;
}

LRESULT WebView::onHorizontalScroll(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    ScrollDirection direction;
    ScrollGranularity granularity;
    switch (LOWORD(wParam)) {
    case SB_LINELEFT:
        granularity = ScrollGranularity::Line;
        direction = ScrollDirection::ScrollLeft;
        break;
    case SB_LINERIGHT:
        granularity = ScrollGranularity::Line;
        direction = ScrollDirection::ScrollRight;
        break;
    case SB_PAGELEFT:
        granularity = ScrollGranularity::Document;
        direction = ScrollDirection::ScrollLeft;
        break;
    case SB_PAGERIGHT:
        granularity = ScrollGranularity::Document;
        direction = ScrollDirection::ScrollRight;
        break;
    default:
        handled = false;
        return 0;
    }

    m_page->scrollBy(direction, granularity);

    handled = true;
    return 0;
}

LRESULT WebView::onVerticalScroll(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    ScrollDirection direction;
    ScrollGranularity granularity;
    switch (LOWORD(wParam)) {
    case SB_LINEDOWN:
        granularity = ScrollGranularity::Line;
        direction = ScrollDirection::ScrollDown;
        break;
    case SB_LINEUP:
        granularity = ScrollGranularity::Line;
        direction = ScrollDirection::ScrollUp;
        break;
    case SB_PAGEDOWN:
        granularity = ScrollGranularity::Document;
        direction = ScrollDirection::ScrollDown;
        break;
    case SB_PAGEUP:
        granularity = ScrollGranularity::Document;
        direction = ScrollDirection::ScrollUp;
        break;
    default:
        handled = false;
        return 0;
    }

    m_page->scrollBy(direction, granularity);

    handled = true;
    return 0;
}

LRESULT WebView::onKeyEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    Vector<MSG> pendingCharEvents;
    if (message == WM_KEYDOWN) {
        MSG msg;
        // WM_SYSCHAR events should not be removed, because WebKit is using WM_SYSCHAR for access keys and they can't be canceled.
        while (PeekMessage(&msg, hWnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE)) {
            if (msg.message == WM_CHAR)
                pendingCharEvents.append(msg);
        }
    }
    m_page->handleKeyboardEvent(NativeWebKeyboardEvent(hWnd, message, wParam, lParam, WTFMove(pendingCharEvents)));

    // We claim here to always have handled the event. If the event is not in fact handled, we will
    // find out later in didNotHandleKeyEvent.
    handled = true;
    return 0;
}

static void drawPageBackground(HDC dc, const WebPageProxy* page, const IntRect& rect)
{
    auto& backgroundColor = page->backgroundColor();
    if (!backgroundColor || backgroundColor.value().isVisible())
        return;

    auto scaledRect = rect;
    scaledRect.scale(page->intrinsicDeviceScaleFactor());
    RECT viewRect = scaledRect;
    ::FillRect(dc, &viewRect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
}

void WebView::paint(HDC hdc, const IntRect& dirtyRect)
{
    if (dirtyRect.isEmpty())
        return;
    m_page->endPrinting();
    if (m_page->drawingArea()) {
        auto painter = [&](auto drawingArea) {
            // FIXME: We should port WebKit1's rect coalescing logic here.
            Region unpaintedRegion;
            auto intrinsicDeviceScaleFactor = m_page->intrinsicDeviceScaleFactor();
#if USE(CAIRO)
            cairo_surface_t* surface = cairo_win32_surface_create(hdc);
            cairo_surface_set_device_scale(surface, intrinsicDeviceScaleFactor, intrinsicDeviceScaleFactor);
            cairo_t* context = cairo_create(surface);

            drawingArea->paint(context, dirtyRect, unpaintedRegion);
    
            cairo_destroy(context);
            cairo_surface_destroy(surface);
#elif USE(SKIA)
            WebCore::IntRect scaledRect = dirtyRect;
            scaledRect.scale(intrinsicDeviceScaleFactor);
            auto info = SkImageInfo::MakeN32Premul(scaledRect.width(), scaledRect.height(), SkColorSpace::MakeSRGB());
            auto surface = SkSurfaces::Raster(info);
            auto canvas = surface->getCanvas();
            canvas->scale(intrinsicDeviceScaleFactor, intrinsicDeviceScaleFactor);
            canvas->translate(-dirtyRect.x(), -dirtyRect.y());

            drawingArea->paint(canvas, dirtyRect, unpaintedRegion);

            SkPixmap pixmap;
            if (surface->peekPixels(&pixmap)) {
                auto bitmapInfo = WebCore::BitmapInfo::createBottomUp({ pixmap.width(), pixmap.height() });
                SetDIBitsToDevice(hdc, scaledRect.x(), scaledRect.y(), pixmap.width(), pixmap.height(), 0, 0, 0, pixmap.height(), pixmap.addr(), &bitmapInfo, DIB_RGB_COLORS);
            }
#endif
    
            auto unpaintedRects = unpaintedRegion.rects();
            for (auto& rect : unpaintedRects)
                drawPageBackground(hdc, m_page.get(), rect);
        };
#if USE(GRAPHICS_LAYER_WC)
        painter(static_cast<DrawingAreaProxyWC*>(m_page->drawingArea()));
#else
        painter(static_cast<DrawingAreaProxyCoordinatedGraphics*>(m_page->drawingArea()));
#endif
    } else
        drawPageBackground(hdc, m_page.get(), dirtyRect);
}

LRESULT WebView::onPaintEvent(HWND hWnd, UINT message, WPARAM, LPARAM, bool& handled)
{
    // Update child windows now so that any areas of our window they reveal will be included in the
    // invalid region that ::BeginPaint sees.

    PAINTSTRUCT paintStruct;
    HDC hdc = ::BeginPaint(m_window, &paintStruct);
    FloatRect dirtyRect(paintStruct.rcPaint);
    dirtyRect.scale(1 / m_page->intrinsicDeviceScaleFactor());
    paint(hdc, enclosingIntRect(dirtyRect));

    ::EndPaint(m_window, &paintStruct);

    handled = true;
    return 0;
}

LRESULT WebView::onPrintClientEvent(HWND hWnd, UINT, WPARAM wParam, LPARAM, bool& handled)
{
    HDC hdc = reinterpret_cast<HDC>(wParam);
    RECT winRect;
    ::GetClientRect(hWnd, &winRect);

    paint(hdc, winRect);

    handled = true;
    return 0;
}

LRESULT WebView::onSizeEvent(HWND hwnd, UINT, WPARAM, LPARAM lParam, bool& handled)
{
    float intrinsicDeviceScaleFactor = deviceScaleFactorForWindow(hwnd);
    if (m_page)
        m_page->setIntrinsicDeviceScaleFactor(intrinsicDeviceScaleFactor);
    m_viewSize = expandedIntSize(FloatSize(LOWORD(lParam), HIWORD(lParam)) / intrinsicDeviceScaleFactor);

    if (m_page && m_page->drawingArea()) {
        // FIXME specify correctly layerPosition.
        m_page->drawingArea()->setSize(m_viewSize, m_nextResizeScrollOffset);
        m_nextResizeScrollOffset = IntSize();
    }

    handled = true;
    return 0;
}

LRESULT WebView::onWindowPositionChangedEvent(HWND, UINT, WPARAM, LPARAM lParam, bool& handled)
{
    if (reinterpret_cast<WINDOWPOS*>(lParam)->flags & SWP_SHOWWINDOW)
        updateActiveStateSoon();

    handled = false;
    return 0;
}

LRESULT WebView::onSetFocusEvent(HWND, UINT, WPARAM, LPARAM lParam, bool& handled)
{
    m_page->activityStateDidChange(ActivityState::IsFocused);
    handled = true;
    return 0;
}

LRESULT WebView::onKillFocusEvent(HWND, UINT, WPARAM, LPARAM lParam, bool& handled)
{
    m_page->activityStateDidChange(ActivityState::IsFocused);
    handled = true;
    return 0;
}

LRESULT WebView::onTimerEvent(HWND hWnd, UINT, WPARAM wParam, LPARAM, bool& handled)
{
    switch (wParam) {
    case UpdateActiveStateTimer:
        ::KillTimer(hWnd, UpdateActiveStateTimer);
        updateActiveState();
        break;
    }

    handled = true;
    return 0;
}

LRESULT WebView::onShowWindowEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    // lParam is 0 when the message is sent because of a ShowWindow call.
    // FIXME: Since we don't get notified when an ancestor window is hidden or shown, we will keep
    // painting even when we have a hidden ancestor. <http://webkit.org/b/54104>
    if (!lParam)
        setIsVisible(wParam);

    handled = false;
    return 0;
}

LRESULT WebView::onSetCursor(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    if (!m_lastCursorSet) {
        handled = false;
        return 0;
    }

    ::SetCursor(m_lastCursorSet);
    return 0;
}

LRESULT WebView::onMenuCommand(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
#if ENABLE(CONTEXT_MENUS)
    auto hMenu = reinterpret_cast<HMENU>(lParam);
    auto index = static_cast<unsigned>(wParam);

    MENUITEMINFO menuItemInfo;
    menuItemInfo.cbSize = sizeof(menuItemInfo);
    menuItemInfo.cch = 0;
    menuItemInfo.fMask = MIIM_STRING;
    ::GetMenuItemInfo(hMenu, index, TRUE, &menuItemInfo);

    menuItemInfo.cch++;
    Vector<WCHAR> buffer(menuItemInfo.cch);
    menuItemInfo.dwTypeData = buffer.mutableSpan().data();
    menuItemInfo.fMask |= MIIM_ID;

    ::GetMenuItemInfo(hMenu, index, TRUE, &menuItemInfo);

    String title(buffer.span().data(), menuItemInfo.cch);
    ContextMenuAction action = static_cast<ContextMenuAction>(menuItemInfo.wID);
    bool enabled = !(menuItemInfo.fState & MFS_DISABLED);
    bool checked = menuItemInfo.fState & MFS_CHECKED;
    WebContextMenuItemData item(ContextMenuItemType::Action, action, WTFMove(title), enabled, checked);
    RefPtr contextMenu = static_cast<WebContextMenuProxyWin*>(m_page->activeContextMenu());
    m_page->contextMenuItemSelected(item, contextMenu->frameInfo());

    handled = true;
#else
    UNUSED_PARAM(hWnd);
    UNUSED_PARAM(message);
    UNUSED_PARAM(wParam);
    UNUSED_PARAM(lParam);
    handled = false;
#endif

    return 0;
}

void WebView::updateActiveState()
{
    m_page->activityStateDidChange(ActivityState::WindowIsActive);
}

void WebView::updateActiveStateSoon()
{
    // This function is called while processing the WM_NCACTIVATE message.
    // While processing WM_NCACTIVATE when we are being deactivated, GetActiveWindow() will
    // still return our window. If we were to call updateActiveState() in that case, we would
    // wrongly think that we are still the active window. To work around this, we update our
    // active state after a 0-delay timer fires, at which point GetActiveWindow() will return
    // the newly-activated window.

    ::SetTimer(m_window, UpdateActiveStateTimer, 0, 0);
}

static bool initCommonControls()
{
    static bool haveInitialized = false;
    if (haveInitialized)
        return true;

    INITCOMMONCONTROLSEX init;
    init.dwSize = sizeof(init);
    init.dwICC = ICC_TREEVIEW_CLASSES;
    haveInitialized = !!::InitCommonControlsEx(&init);
    return haveInitialized;
}

void WebView::initializeToolTipWindow()
{
    if (!initCommonControls())
        return;

    m_toolTipWindow = ::CreateWindowEx(WS_EX_TRANSPARENT, TOOLTIPS_CLASS, 0, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_window, 0, 0, 0);
    if (!m_toolTipWindow)
        return;

    TOOLINFO info { };
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.uId = reinterpret_cast<UINT_PTR>(m_window);

    ::SendMessage(m_toolTipWindow, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&info));
    ::SendMessage(m_toolTipWindow, TTM_SETMAXTIPWIDTH, 0, kMaxToolTipWidth);
    ::SetWindowPos(m_toolTipWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void WebView::startTrackingMouseLeave()
{
    if (m_trackingMouseLeave)
        return;
    m_trackingMouseLeave = true;

    TRACKMOUSEEVENT trackMouseEvent;
    trackMouseEvent.cbSize = sizeof(TRACKMOUSEEVENT);
    trackMouseEvent.dwFlags = TME_LEAVE;
    trackMouseEvent.hwndTrack = m_window;

    ::TrackMouseEvent(&trackMouseEvent);
}

void WebView::stopTrackingMouseLeave()
{
    if (!m_trackingMouseLeave)
        return;
    m_trackingMouseLeave = false;

    TRACKMOUSEEVENT trackMouseEvent;
    trackMouseEvent.cbSize = sizeof(TRACKMOUSEEVENT);
    trackMouseEvent.dwFlags = TME_LEAVE | TME_CANCEL;
    trackMouseEvent.hwndTrack = m_window;

    ::TrackMouseEvent(&trackMouseEvent);
}

bool WebView::shouldInitializeTrackPointHack()
{
    static bool shouldCreateScrollbars;
    static bool hasRunTrackPointCheck;

    if (hasRunTrackPointCheck)
        return shouldCreateScrollbars;

    hasRunTrackPointCheck = true;
    const wchar_t* trackPointKeys[] = {
        L"Software\\Lenovo\\TrackPoint",
        L"Software\\Lenovo\\UltraNav",
        L"Software\\Alps\\Apoint\\TrackPoint",
        L"Software\\Synaptics\\SynTPEnh\\UltraNavUSB",
        L"Software\\Synaptics\\SynTPEnh\\UltraNavPS2"
    };

    for (size_t i = 0; i < std::size(trackPointKeys); ++i) {
        HKEY trackPointKey;
        int readKeyResult = ::RegOpenKeyExW(HKEY_CURRENT_USER, trackPointKeys[i], 0, KEY_READ, &trackPointKey);
        ::RegCloseKey(trackPointKey);
        if (readKeyResult == ERROR_SUCCESS) {
            shouldCreateScrollbars = true;
            return shouldCreateScrollbars;
        }
    }

    return shouldCreateScrollbars;
}

void WebView::close()
{
    if (m_window && !m_isBeingDestroyed)
        DestroyWindow(m_window);
}

void WebView::closeInternal()
{
    m_window = 0;
    setParentWindow(0);
    m_page->close();
}

HCURSOR WebView::cursorToShow() const
{
    if (!m_page->hasRunningProcess())
        return 0;

    // We only show the override cursor if the default (arrow) cursor is showing.
    static HCURSOR arrowCursor = ::LoadCursor(0, IDC_ARROW);
    if (m_overrideCursor && m_webCoreCursor == arrowCursor)
        return m_overrideCursor;

    return m_webCoreCursor;
}

void WebView::setCursor(const WebCore::Cursor& cursor)
{
    if (!cursor.platformCursor()->nativeCursor())
        return;
    m_webCoreCursor = cursor.platformCursor()->nativeCursor();
    updateNativeCursor();
}

void WebView::updateNativeCursor()
{
    m_lastCursorSet = cursorToShow();
    if (!m_lastCursorSet)
        return;
    ::SetCursor(m_lastCursorSet);
}

void WebView::setOverrideCursor(HCURSOR overrideCursor)
{
    m_overrideCursor = overrideCursor;
    updateNativeCursor();
}

void WebView::setIsInWindow(bool isInWindow)
{
    m_isInWindow = isInWindow;
    if (m_page)
        m_page->activityStateDidChange(ActivityState::IsInWindow);
}

void WebView::setIsVisible(bool isVisible)
{
    m_isVisible = isVisible;
    if (m_page)
        m_page->activityStateDidChange(ActivityState::IsVisible);
}

bool WebView::isWindowActive()
{
    HWND activeWindow = ::GetActiveWindow();
    return (activeWindow && m_topLevelParentWindow == findTopLevelParentWindow(activeWindow));
}

bool WebView::isFocused()
{
    return ::GetFocus() == m_window;
}

bool WebView::isVisible()
{
    return m_isVisible;
}

bool WebView::isInWindow()
{
    return m_isInWindow;
}

void WebView::setScrollOffsetOnNextResize(const IntSize& scrollOffset)
{
    // The next time we get a WM_SIZE message, scroll by the specified amount in onSizeEvent().
    m_nextResizeScrollOffset = scrollOffset;
    m_nextResizeScrollOffset.scale(1 / m_page->intrinsicDeviceScaleFactor());
}

void WebView::setViewNeedsDisplay(const WebCore::Region& region)
{
    auto rect = region.bounds();
    rect.scale(m_page->intrinsicDeviceScaleFactor());
    const RECT viewRect(rect);
    ::InvalidateRect(m_window, &viewRect, true);
}

void WebView::didCommitLoadForMainFrame(bool useCustomRepresentation)
{
}

double WebView::customRepresentationZoomFactor()
{
    return 1;
}

void WebView::setCustomRepresentationZoomFactor(double)
{
}

void WebView::findStringInCustomRepresentation(const String&, FindOptions, unsigned)
{
}

void WebView::countStringMatchesInCustomRepresentation(const String&, FindOptions, unsigned)
{
}

HWND WebView::nativeWindow()
{
    return m_window;
}

// WebCore::WindowMessageListener

void WebView::windowReceivedMessage(HWND, UINT message, WPARAM wParam, LPARAM)
{
    switch (message) {
    case WM_NCACTIVATE:
        updateActiveStateSoon();
        break;
    case WM_SETTINGCHANGE:
        break;
    }
}

static Vector<wchar_t> truncatedString(const String& string)
{
    // Truncate tooltip texts because multiline mode of tooltip control does word-wrapping very slowly
    size_t maxLength = 1024;
    auto buffer = string.wideCharacters();
    if (buffer.size() > maxLength) {
        buffer[maxLength - 4] = L'.';
        buffer[maxLength - 3] = L'.';
        buffer[maxLength - 2] = L'.';
        buffer[maxLength - 1] = L'\0';
    }
    return buffer;
}

void WebView::setToolTip(const String& toolTip)
{
    if (!m_toolTipWindow)
        return;

    if (!toolTip.isEmpty()) {
        TOOLINFO info { };
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND;
        info.uId = reinterpret_cast<UINT_PTR>(nativeWindow());
        auto toolTipCharacters = truncatedString(toolTip); // Retain buffer long enough to make the SendMessage call
        info.lpszText = toolTipCharacters.mutableSpan().data();
        ::SendMessage(m_toolTipWindow, TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&info));
    }

    ::SendMessage(m_toolTipWindow, TTM_ACTIVATE, !toolTip.isEmpty(), 0);
}

void WebView::setUsesOffscreenRendering(bool enabled)
{
    m_usesOffscreenRendering = enabled;
}

bool WebView::usesOffscreenRendering() const
{
    return m_usesOffscreenRendering;
}

} // namespace WebKit
