/*
 * Copyright (C) 2019 Sony Interactive Entertainment Inc.
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
#include "RemoteInspectorProtocolHandler.h"

#if ENABLE(REMOTE_INSPECTOR)

#include "APIContentWorld.h"
#include "APILoaderClient.h"
#include "APINavigation.h"
#include "APIPageConfiguration.h"
#include "APISerializedScriptValue.h"
#include "JavaScriptEvaluationResult.h"
#include "PageLoadState.h"
#include "RunJavaScriptParameters.h"
#include "WebPageGroup.h"
#include "WebPageProxy.h"
#include "WebScriptMessageHandler.h"
#include "WebUserContentControllerProxy.h"
#include <WebCore/JSDOMExceptionHandling.h>
#include <WebCore/RunJavaScriptParameters.h>
#include <WebCore/SerializedScriptValue.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/StringToIntegerConversion.h>

namespace WebKit {

using namespace WebCore;

class ScriptMessageClient final : public WebScriptMessageHandler::Client {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(X);
public:
    ScriptMessageClient(RemoteInspectorProtocolHandler& inspectorProtocolHandler)
        : m_inspectorProtocolHandler(inspectorProtocolHandler) { }

    ~ScriptMessageClient() { }

    void didPostMessage(WebPageProxy& page, FrameInfoData&&, API::ContentWorld&, JavaScriptEvaluationResult&& jsMessage) override
    {
        auto valueAsString = jsMessage.toString();
        auto tokens = StringView { valueAsString }.split(':');
        uint32_t connectionID = 0;
        uint32_t targetID = 0;
        String type;
        int i = 0;
        for (auto token : tokens) {
            if (!i)
                connectionID = parseInteger<uint32_t>(token).value_or(0);
            else if (i == 1)
                targetID = parseInteger<uint32_t>(token).value_or(0);
            else if (i == 2)
                type = token.toString();
            else
                return;
            ++i;
        }
        if (i != 3)
            return;

        URL requestURL { page.pageLoadState().url() };
        m_inspectorProtocolHandler->inspect(requestURL.hostAndPort(), connectionID, targetID, type);
    }
    
    bool supportsAsyncReply() override
    {
        return false;
    }
    
    void didPostMessageWithAsyncReply(WebPageProxy&, FrameInfoData&&, API::ContentWorld&, JavaScriptEvaluationResult&&, WTF::Function<void(Expected<JavaScriptEvaluationResult, String>&&)>&&) override
    {
    }

private:
    CheckedRef<RemoteInspectorProtocolHandler> m_inspectorProtocolHandler;
};

class LoaderClient final : public API::LoaderClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(LoaderClient);
public:
    LoaderClient(Function<void()>&& loadedCallback)
        : m_loadedCallback { WTFMove(loadedCallback) } { }

    void didFinishLoadForFrame(WebKit::WebPageProxy&, WebKit::WebFrameProxy&, API::Navigation*, API::Object*) final
    {
        m_loadedCallback();
    }

private:
    Function<void()> m_loadedCallback;
};

static std::optional<Inspector::DebuggableType> parseDebuggableTypeFromString(const String& debuggableTypeString)
{
    if (debuggableTypeString == "itml"_s)
        return Inspector::DebuggableType::ITML;
    if (debuggableTypeString == "javascript"_s)
        return Inspector::DebuggableType::JavaScript;
    if (debuggableTypeString == "page"_s)
        return Inspector::DebuggableType::Page;
    if (debuggableTypeString == "service-worker"_s)
        return Inspector::DebuggableType::ServiceWorker;
    if (debuggableTypeString == "web-page"_s)
        return Inspector::DebuggableType::WebPage;

    return std::nullopt;
}

void RemoteInspectorProtocolHandler::inspect(const String& hostAndPort, ConnectionID connectionID, TargetID targetID, const String& type)
{
    auto debuggableType = parseDebuggableTypeFromString(type);
    if (!debuggableType) {
        LOG_ERROR("Unknown debuggable type: \"%s\"", type.utf8().data());
        return;
    }

    if (m_inspectorClient)
        m_inspectorClient->inspect(connectionID, targetID, debuggableType.value());
}

Ref<WebPageProxy> RemoteInspectorProtocolHandler::protectedPage() const
{
    return m_page.get();
}

void RemoteInspectorProtocolHandler::runScript(const String& script)
{
    constexpr bool wantsResult = true;
    protectedPage()->runJavaScriptInMainFrame(WebKit::RunJavaScriptParameters {
        script,
        JSC::SourceTaintedOrigin::Untainted,
        URL { },
        WebCore::RunAsAsyncFunction::No,
        std::nullopt,
        WebCore::ForceUserGesture::No,
        RemoveTransientActivation::Yes
    }, wantsResult, [] (auto&& result) {
        if (!result && result.error())
            LOG_ERROR("Exception running script \"%s\"", result.error()->message.utf8().data());
    });
}

void RemoteInspectorProtocolHandler::targetListChanged(RemoteInspectorClient& client)
{
    StringBuilder html;
    if (client.targets().isEmpty())
        html.append("<p>No targets found</p>"_s);
    else {
        html.append("<table>"_s);
        for (auto& connectionID : client.targets().keys()) {
            for (auto& target : client.targets().get(connectionID)) {
                html.append(makeString(
                    "<tbody><tr>"
                    "<td class=\"data\"><div class=\"targetname\">"_s, target.name, "</div><div class=\"targeturl\">"_s, target.url, "</div></td>"
                    "<td class=\"input\"><input type=\"button\" value=\"Inspect\" onclick=\"window.webkit.messageHandlers.inspector.postMessage(\\'"_s, connectionID, ':', target.id, ':', target.type, "\\');\"></td>"
                    "</tr></tbody>"_s
                ));
            }
        }
        html.append("</table>"_s);
    }
    m_targetListsHtml = html.toString();
    if (m_pageLoaded)
        updateTargetList();
}

void RemoteInspectorProtocolHandler::updateTargetList()
{
    if (!m_targetListsHtml.isEmpty()) {
        runScript(makeString("updateTargets(`"_s, m_targetListsHtml, "`);"_s));
        m_targetListsHtml = { };
    }
}

void RemoteInspectorProtocolHandler::platformStartTask(WebPageProxy& pageProxy, WebURLSchemeTask& task)
{
    auto requestURL = task.request().url();

    // Destroy the client before creating a new connection so it can connect to the same port
    m_inspectorClient = nullptr;
    m_inspectorClient = makeUnique<RemoteInspectorClient>(requestURL, *this);

    // Setup target postMessage listener
    auto handler = WebScriptMessageHandler::create(makeUnique<ScriptMessageClient>(*this), "inspector"_s, API::ContentWorld::pageContentWorldSingleton());
    pageProxy.configuration().userContentController().addUserScriptMessageHandler(handler.get());

    // Setup loader client to get notified of page load
    protectedPage()->setLoaderClient(makeUnique<LoaderClient>([this] {
        m_pageLoaded = true;
        updateTargetList();
    }));
    m_pageLoaded = false;

    StringBuilder htmlBuilder;
    htmlBuilder.append(
        "<html><head><title>Remote Inspector</title>"
        "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />"
        "<style>"
        "  h1 { color: #babdb6; text-shadow: 0 1px 0 white; margin-bottom: 0; }"
        "  html { font-family: -webkit-system-font; font-size: 11pt; color: #2e3436; padding: 20px 20px 0 20px; background-color: #f6f6f4; "
        "         background-image: -webkit-gradient(linear, left top, left bottom, color-stop(0, #eeeeec), color-stop(1, #f6f6f4));"
        "         background-size: 100% 5em; background-repeat: no-repeat; }"
        "  table { width: 100%; border-collapse: collapse; table-layout: fixed; }"
        "  table, td { border: 1px solid #d3d7cf; border-left: none; border-right: none; }"
        "  p { margin-bottom: 30px; }"
        "  td { padding: 15px; }"
        "  td.data { width: 200px; }"
        "  .targetname { font-weight: bold; overflow: hidden; white-space:nowrap; text-overflow: ellipsis; }"
        "  .targeturl { color: #babdb6; background: #eee; word-wrap: break-word; overflow-wrap: break-word; }"
        "  td.input { width: 64px; }"
        "  input { width: 100%; padding: 8px; }"
        "</style>"
        "</head><body><h1>Inspectable targets</h1>"
        "<div id=\"targetlist\"><p>No targets found</p></div></body>"
        "<script>"
        "function updateTargets(str) {"
            "let targetDiv = document.getElementById('targetlist');"
            "targetDiv.innerHTML = str;"
        "}"
        "</script>"_s);
    htmlBuilder.append("</html>"_s);

    auto html = htmlBuilder.toString().utf8();
    auto data = SharedBuffer::create(html.span());
    ResourceResponse response(WTFMove(requestURL), "text/html"_s, html.length(), "UTF-8"_s);
    task.didReceiveResponse(WTFMove(response));
    task.didReceiveData(WTFMove(data));
    task.didComplete(ResourceError());
}

} // namespace WebKit

#endif // ENABLE(REMOTE_INSPECTOR)
