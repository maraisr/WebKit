/*
 * Copyright (C) 2008-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
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
 *
 */

#include "config.h"
#include "ScriptExecutionContext.h"

#include "CSSValuePool.h"
#include "CachedScript.h"
#include "CommonVM.h"
#include "ContentSecurityPolicy.h"
#include "CrossOriginMode.h"
#include "CrossOriginOpenerPolicy.h"
#include "DocumentInlines.h"
#include "DOMTimer.h"
#include "DatabaseContext.h"
#include "Document.h"
#include "EmptyScriptExecutionContext.h"
#include "ErrorEvent.h"
#include "FontLoadRequest.h"
#include "FrameDestructionObserverInlines.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMPromiseDeferred.h"
#include "JSWorkerGlobalScope.h"
#include "JSWorkletGlobalScope.h"
#include "LegacySchemeRegistry.h"
#include "LocalDOMWindow.h"
#include "MessagePort.h"
#include "Navigator.h"
#include "OriginAccessPatterns.h"
#include "Page.h"
#include "Performance.h"
#include "PublicURLManager.h"
#include "RTCDataChannelRemoteHandlerConnection.h"
#include "RejectedPromiseTracker.h"
#include "ResourceRequest.h"
#include "SWClientConnection.h"
#include "SWContextManager.h"
#include "ScriptController.h"
#include "ScriptDisallowedScope.h"
#include "ScriptExecutionContextInlines.h"
#include "ScriptTrackingPrivacyCategory.h"
#include "ServiceWorker.h"
#include "ServiceWorkerGlobalScope.h"
#include "ServiceWorkerProvider.h"
#include "Settings.h"
#include "WebCoreJSClientData.h"
#include "WebCoreOpaqueRoot.h"
#include "WorkerGlobalScope.h"
#include "WorkerLoaderProxy.h"
#include "WorkerNavigator.h"
#include "WorkerOrWorkletGlobalScope.h"
#include "WorkerOrWorkletScriptController.h"
#include "WorkerOrWorkletThread.h"
#include "WorkerThread.h"
#include "WorkletGlobalScope.h"
#include <JavaScriptCore/CallFrame.h>
#include <JavaScriptCore/CatchScope.h>
#include <JavaScriptCore/DeferredWorkTimer.h>
#include <JavaScriptCore/Exception.h>
#include <JavaScriptCore/JSPromise.h>
#include <JavaScriptCore/ScriptCallStack.h>
#include <JavaScriptCore/SourceTaintedOrigin.h>
#include <JavaScriptCore/StackVisitor.h>
#include <JavaScriptCore/StrongInlines.h>
#include <JavaScriptCore/VM.h>
#include <wtf/Lock.h>
#include <wtf/MainThread.h>
#include <wtf/Ref.h>
#include <wtf/SetForScope.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace Inspector;

static std::atomic<CrossOriginMode> globalCrossOriginMode { CrossOriginMode::Shared };

static Lock allScriptExecutionContextsMapLock;
static HashMap<ScriptExecutionContextIdentifier, ScriptExecutionContext*>& allScriptExecutionContextsMap() WTF_REQUIRES_LOCK(allScriptExecutionContextsMapLock)
{
    static NeverDestroyed<HashMap<ScriptExecutionContextIdentifier, ScriptExecutionContext*>> contexts;
    ASSERT(allScriptExecutionContextsMapLock.isLocked());
    return contexts;
}

struct ScriptExecutionContext::PendingException {
    WTF_MAKE_TZONE_ALLOCATED(PendingException);
public:
    PendingException(const String& errorMessage, int lineNumber, int columnNumber, const String& sourceURL, RefPtr<ScriptCallStack>&& callStack)
        : m_errorMessage(errorMessage)
        , m_lineNumber(lineNumber)
        , m_columnNumber(columnNumber)
        , m_sourceURL(sourceURL)
        , m_callStack(WTFMove(callStack))
    {
    }
    String m_errorMessage;
    int m_lineNumber;
    int m_columnNumber;
    String m_sourceURL;
    RefPtr<ScriptCallStack> m_callStack;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(ScriptExecutionContext::PendingException);
WTF_MAKE_TZONE_ALLOCATED_IMPL(ScriptExecutionContext::Task);

ScriptExecutionContext::ScriptExecutionContext(Type type, std::optional<ScriptExecutionContextIdentifier> contextIdentifier)
    : m_identifier(contextIdentifier ? *contextIdentifier : ScriptExecutionContextIdentifier::generate())
    , m_storageBlockingPolicy { StorageBlockingPolicy::AllowAll }
    , m_type(type)
{
}

std::unique_ptr<ContentSecurityPolicy> ScriptExecutionContext::makeEmptyContentSecurityPolicy()
{
    return makeUnique<ContentSecurityPolicy>(URL { emptyString() }, *this);
}

void ScriptExecutionContext::regenerateIdentifier()
{
    Locker locker { allScriptExecutionContextsMapLock };

    ASSERT(allScriptExecutionContextsMap().contains(m_identifier));
    allScriptExecutionContextsMap().remove(m_identifier);

    m_identifier = ScriptExecutionContextIdentifier::generate();

    ASSERT(!allScriptExecutionContextsMap().contains(m_identifier));
    allScriptExecutionContextsMap().add(m_identifier, this);
}

void ScriptExecutionContext::addToContextsMap()
{
    Locker locker { allScriptExecutionContextsMapLock };
    ASSERT(!allScriptExecutionContextsMap().contains(m_identifier));
    allScriptExecutionContextsMap().add(m_identifier, this);
}

void ScriptExecutionContext::removeFromContextsMap()
{
    Locker locker { allScriptExecutionContextsMapLock };
    ASSERT(allScriptExecutionContextsMap().contains(m_identifier));
    allScriptExecutionContextsMap().remove(m_identifier);
}

#if !ASSERT_ENABLED

inline void ScriptExecutionContext::checkConsistency() const
{
}

#else // ASSERT_ENABLED

void ScriptExecutionContext::checkConsistency() const
{
    for (auto* messagePort : m_messagePorts)
        ASSERT(messagePort->scriptExecutionContext() == this);

    for (auto* destructionObserver : m_destructionObservers)
        ASSERT(destructionObserver->scriptExecutionContext() == this);

    // This can run on the GC thread.
    for (SUPPRESS_UNCOUNTED_LOCAL auto* activeDOMObject : m_activeDOMObjects) {
        ASSERT(activeDOMObject->scriptExecutionContext() == this);
        activeDOMObject->assertSuspendIfNeededWasCalled();
    }
}

#endif // ASSERT_ENABLED

ScriptExecutionContext::~ScriptExecutionContext()
{
    checkConsistency();

#if ASSERT_ENABLED
    {
        Locker locker { allScriptExecutionContextsMapLock };
        ASSERT_WITH_MESSAGE(!allScriptExecutionContextsMap().contains(m_identifier), "A ScriptExecutionContext subclass instance implementing postTask should have already removed itself from the map");
    }

    m_inScriptExecutionContextDestructor = true;
#endif // ASSERT_ENABLED

    auto callbacks = WTFMove(m_notificationCallbacks);
    for (auto& callback : callbacks.values())
        callback();

    auto postMessageCompletionHandlers = WTFMove(m_processMessageWithMessagePortsSoonHandlers);
    for (auto& completionHandler : postMessageCompletionHandlers)
        completionHandler();

    setActiveServiceWorker(nullptr);

    while (auto* destructionObserver = m_destructionObservers.takeAny())
        destructionObserver->contextDestroyed();

    setContentSecurityPolicy(nullptr);

#if ASSERT_ENABLED
    m_inScriptExecutionContextDestructor = false;
#endif
}

void ScriptExecutionContext::processMessageWithMessagePortsSoon(CompletionHandler<void()>&& completionHandler)
{
    ASSERT(isContextThread());
    m_processMessageWithMessagePortsSoonHandlers.append(WTFMove(completionHandler));

    if (m_willprocessMessageWithMessagePortsSoon)
        return;

    m_willprocessMessageWithMessagePortsSoon = true;
    postTask([] (ScriptExecutionContext& context) {
        context.dispatchMessagePortEvents();
    });
}

void ScriptExecutionContext::dispatchMessagePortEvents()
{
    ASSERT(isContextThread());
    checkConsistency();

    Ref<ScriptExecutionContext> protectedThis(*this);
    ASSERT(m_willprocessMessageWithMessagePortsSoon);
    m_willprocessMessageWithMessagePortsSoon = false;

    auto completionHandlers = std::exchange(m_processMessageWithMessagePortsSoonHandlers, Vector<CompletionHandler<void()>> { });

    // Make a frozen copy of the ports so we can iterate while new ones might be added or destroyed.
    for (RefPtr messagePort : copyToVectorOf<RefPtr<MessagePort>>(m_messagePorts)) {
        // The port may be destroyed, and another one created at the same address,
        // but this is harmless. The worst that can happen as a result is that
        // dispatchMessages() will be called needlessly.
        if (m_messagePorts.contains(messagePort.get()) && messagePort->started())
            messagePort->dispatchMessages();
    }

    for (auto& completionHandler : completionHandlers)
        completionHandler();
}

void ScriptExecutionContext::createdMessagePort(MessagePort& messagePort)
{
    ASSERT(isContextThread());

    m_messagePorts.add(&messagePort);
}

void ScriptExecutionContext::destroyedMessagePort(MessagePort& messagePort)
{
    ASSERT(isContextThread());

    m_messagePorts.remove(&messagePort);
}

void ScriptExecutionContext::didLoadResourceSynchronously(const URL&)
{
}

CSSValuePool& ScriptExecutionContext::cssValuePool()
{
    return CSSValuePool::singleton();
}

std::unique_ptr<FontLoadRequest> ScriptExecutionContext::fontLoadRequest(const String&, bool, bool, LoadedFromOpaqueSource)
{
    return nullptr;
}

void ScriptExecutionContext::forEachActiveDOMObject(NOESCAPE const Function<ShouldContinue(ActiveDOMObject&)>& apply) const
{
    // It is not allowed to run arbitrary script or construct new ActiveDOMObjects while we are iterating over ActiveDOMObjects.
    // An ASSERT_WITH_SECURITY_IMPLICATION or RELEASE_ASSERT will fire if this happens, but it's important to code
    // suspend() / resume() / stop() functions so it will not happen!
    ScriptDisallowedScope scriptDisallowedScope;
    SetForScope activeDOMObjectAdditionForbiddenScope(m_activeDOMObjectAdditionForbidden, true);

    // Make a frozen copy of the objects so we can iterate while new ones might be destroyed.
    auto possibleActiveDOMObjects = copyToVectorOf<RefPtr<ActiveDOMObject>>(m_activeDOMObjects);

    for (auto& activeDOMObject : possibleActiveDOMObjects) {
        // Check if this object was deleted already. If so, just skip it.
        // Calling contains on a possibly-already-deleted object is OK because we guarantee
        // no new object can be added, so even if a new object ends up allocated with the
        // same address, that will be *after* this function exits.
        if (!m_activeDOMObjects.contains(activeDOMObject.get()))
            continue;

        if (apply(*activeDOMObject) == ShouldContinue::No)
            break;
    }
}

JSC::ScriptExecutionStatus ScriptExecutionContext::jscScriptExecutionStatus() const
{
    if (activeDOMObjectsAreSuspended())
        return JSC::ScriptExecutionStatus::Suspended;
    if (activeDOMObjectsAreStopped())
        return JSC::ScriptExecutionStatus::Stopped;
    return JSC::ScriptExecutionStatus::Running;
}

URL ScriptExecutionContext::currentSourceURL(CallStackPosition position) const
{
    auto* globalObject = this->globalObject();
    if (!globalObject)
        return { };

    auto& vm = globalObject->vm();
    auto* topCallFrame = vm.topCallFrame;
    if (!topCallFrame)
        return { };

    URL sourceURL;
    JSC::StackVisitor::visit(topCallFrame, vm, [&sourceURL, position](auto& visitor) {
        if (visitor->isNativeFrame())
            return IterationStatus::Continue;

        auto urlString = visitor->sourceURL();
        if (urlString.isEmpty())
            return IterationStatus::Continue;

        auto newSourceURL = URL { WTFMove(urlString) };
        if (!newSourceURL.isValid())
            return IterationStatus::Continue;

        sourceURL = WTFMove(newSourceURL);
        return position == CallStackPosition::BottomMost ? IterationStatus::Continue : IterationStatus::Done;
    });
    return sourceURL;
}

void ScriptExecutionContext::suspendActiveDOMObjects(ReasonForSuspension why)
{
    checkConsistency();

    if (m_activeDOMObjectsAreSuspended) {
        // A page may subsequently suspend DOM objects, say as part of entering the back/forward cache, after the embedding
        // client requested the page be suspended. We ignore such requests so long as the embedding client requested
        // the suspension first. See <rdar://problem/13754896> for more details.
        ASSERT(m_reasonForSuspendingActiveDOMObjects == ReasonForSuspension::PageWillBeSuspended);
        return;
    }

    m_activeDOMObjectsAreSuspended = true;

    forEachActiveDOMObject([why](auto& activeDOMObject) {
        activeDOMObject.suspend(why);
        return ShouldContinue::Yes;
    });

    m_reasonForSuspendingActiveDOMObjects = why;
}

void ScriptExecutionContext::resumeActiveDOMObjects(ReasonForSuspension why)
{
    checkConsistency();

    if (m_reasonForSuspendingActiveDOMObjects != why)
        return;

    forEachActiveDOMObject([](auto& activeDOMObject) {
        activeDOMObject.resume();
        return ShouldContinue::Yes;
    });

    vm().deferredWorkTimer->didResumeScriptExecutionOwner();

    m_activeDOMObjectsAreSuspended = false;

    // In case there were pending messages at the time the script execution context entered the BackForwardCache,
    // make sure those get dispatched shortly after restoring from the BackForwardCache.
    processMessageWithMessagePortsSoon([] { });
}

void ScriptExecutionContext::stopActiveDOMObjects()
{
    checkConsistency();

    if (m_activeDOMObjectsAreStopped)
        return;
    m_activeDOMObjectsAreStopped = true;

    forEachActiveDOMObject([](auto& activeDOMObject) {
        activeDOMObject.stop();
        return ShouldContinue::Yes;
    });

    m_nativePromiseRequests.forEach([] (auto& request) {
        request.disconnect();
    });
}

void ScriptExecutionContext::suspendActiveDOMObjectIfNeeded(ActiveDOMObject& activeDOMObject)
{
    ASSERT(m_activeDOMObjects.contains(&activeDOMObject));
    if (m_activeDOMObjectsAreSuspended)
        activeDOMObject.suspend(m_reasonForSuspendingActiveDOMObjects);
    if (m_activeDOMObjectsAreStopped)
        activeDOMObject.stop();
}

void ScriptExecutionContext::didCreateActiveDOMObject(ActiveDOMObject& activeDOMObject)
{
    // The m_activeDOMObjectAdditionForbidden check is a RELEASE_ASSERT because of the
    // consequences of having an ActiveDOMObject that is not correctly reflected in the set.
    // If we do have one of those, it can possibly be a security vulnerability. So we'd
    // rather have a crash than continue running with the set possibly compromised.
    ASSERT(!m_inScriptExecutionContextDestructor);
    RELEASE_ASSERT(!m_activeDOMObjectAdditionForbidden);
    m_activeDOMObjects.add(&activeDOMObject);
}

void ScriptExecutionContext::willDestroyActiveDOMObject(ActiveDOMObject& activeDOMObject)
{
    m_activeDOMObjects.remove(&activeDOMObject);
}

void ScriptExecutionContext::didCreateDestructionObserver(ContextDestructionObserver& observer)
{
    ASSERT(!m_inScriptExecutionContextDestructor);
    m_destructionObservers.add(&observer);
}

void ScriptExecutionContext::willDestroyDestructionObserver(ContextDestructionObserver& observer)
{
    m_destructionObservers.remove(&observer);
}

std::optional<PAL::SessionID> ScriptExecutionContext::sessionID() const
{
    return std::nullopt;
}

RefPtr<RTCDataChannelRemoteHandlerConnection> ScriptExecutionContext::createRTCDataChannelRemoteHandlerConnection()
{
    return nullptr;
}

// FIXME: Should this function be in SecurityContext or SecurityOrigin instead?
bool ScriptExecutionContext::canIncludeErrorDetails(CachedScript* script, const String& sourceURL, bool fromModule)
{
    ASSERT(securityOrigin());
    // Errors from module scripts are never muted.
    if (fromModule)
        return true;
    URL completeSourceURL = completeURL(sourceURL);
    if (completeSourceURL.protocolIsData())
        return true;
    if (script) {
        ASSERT(script->origin());
        ASSERT(securityOrigin()->toString() == script->origin()->toString());
        return script->isCORSSameOrigin();
    }
    return protectedSecurityOrigin()->canRequest(completeSourceURL, OriginAccessPatternsForWebProcess::singleton());
}

void ScriptExecutionContext::reportException(const String& errorMessage, int lineNumber, int columnNumber, const String& sourceURL, JSC::Exception* exception, RefPtr<ScriptCallStack>&& callStack, CachedScript* cachedScript, bool fromModule)
{
    if (m_inDispatchErrorEvent) {
        if (!m_pendingExceptions)
            m_pendingExceptions = makeUnique<Vector<std::unique_ptr<PendingException>>>();
        m_pendingExceptions->append(makeUnique<PendingException>(errorMessage, lineNumber, columnNumber, sourceURL, WTFMove(callStack)));
        return;
    }

    // First report the original exception and only then all the nested ones.
    if (!dispatchErrorEvent(errorMessage, lineNumber, columnNumber, sourceURL, exception, cachedScript, fromModule))
        logExceptionToConsole(errorMessage, sourceURL, lineNumber, columnNumber, callStack.copyRef());

    if (!m_pendingExceptions)
        return;

    auto pendingExceptions = WTFMove(m_pendingExceptions);
    for (auto& exception : *pendingExceptions)
        logExceptionToConsole(exception->m_errorMessage, exception->m_sourceURL, exception->m_lineNumber, exception->m_columnNumber, WTFMove(exception->m_callStack));
}

void ScriptExecutionContext::reportUnhandledPromiseRejection(JSC::JSGlobalObject& state, JSC::JSPromise& promise, RefPtr<Inspector::ScriptCallStack>&& callStack)
{
    Page* page = nullptr;
    if (auto* document = dynamicDowncast<Document>(*this))
        page = document->page();
    // FIXME: allow Workers to mute unhandled promise rejection messages.

    if (page && !page->settings().unhandledPromiseRejectionToConsoleEnabled())
        return;

    Ref vm = state.vm();
    auto scope = DECLARE_CATCH_SCOPE(vm);
    JSC::JSValue result = promise.result(vm);
    String resultMessage = retrieveErrorMessage(state, vm, result, scope);
    String errorMessage;

    auto tryMakeErrorString = [&] (unsigned length) -> String {
        bool addEllipsis = length != resultMessage.length();
        return tryMakeString("Unhandled Promise Rejection: "_s, StringView(resultMessage).left(length), addEllipsis ? "..."_s : ""_s);
    };

    if (!!resultMessage && !scope.exception()) {
        constexpr unsigned maxLength = 200;
        constexpr unsigned shortLength = 10;
        errorMessage = tryMakeErrorString(std::min(resultMessage.length(), maxLength));
        if (!errorMessage && resultMessage.length() > shortLength)
            errorMessage = tryMakeErrorString(shortLength);
    }

    if (!errorMessage)
        errorMessage = "Unhandled Promise Rejection"_s;

    std::unique_ptr<Inspector::ConsoleMessage> message;
    if (callStack)
        message = makeUnique<Inspector::ConsoleMessage>(MessageSource::JS, MessageType::Log, MessageLevel::Error, errorMessage, callStack.releaseNonNull());
    else
        message = makeUnique<Inspector::ConsoleMessage>(MessageSource::JS, MessageType::Log, MessageLevel::Error, errorMessage);
    addConsoleMessage(WTFMove(message));
}

void ScriptExecutionContext::addConsoleMessage(MessageSource source, MessageLevel level, const String& message, const String& sourceURL, unsigned lineNumber, unsigned columnNumber, JSC::JSGlobalObject* state, unsigned long requestIdentifier)
{
    addMessage(source, level, message, sourceURL, lineNumber, columnNumber, nullptr, state, requestIdentifier);
}

Ref<SecurityOrigin> ScriptExecutionContext::protectedTopOrigin() const
{
    return topOrigin();
}

bool ScriptExecutionContext::dispatchErrorEvent(const String& errorMessage, int lineNumber, int columnNumber, const String& sourceURL, JSC::Exception* exception, CachedScript* cachedScript, bool fromModule)
{
    RefPtr target = errorEventTarget();
    if (!target)
        return false;

    RefPtr<ErrorEvent> errorEvent;
    if (canIncludeErrorDetails(cachedScript, sourceURL, fromModule))
        errorEvent = ErrorEvent::create(errorMessage, sourceURL, lineNumber, columnNumber, { vm(), exception ? exception->value() : JSC::jsNull() });
    else
        errorEvent = ErrorEvent::create("Script error."_s, { }, 0, 0, { });

    ASSERT(!m_inDispatchErrorEvent);
    m_inDispatchErrorEvent = true;
    target->dispatchEvent(*errorEvent);
    m_inDispatchErrorEvent = false;
    return errorEvent->defaultPrevented();
}

int ScriptExecutionContext::circularSequentialID()
{
    ++m_circularSequentialID;
    if (m_circularSequentialID <= 0)
        m_circularSequentialID = 1;
    return m_circularSequentialID;
}

Ref<JSC::VM> ScriptExecutionContext::protectedVM()
{
    return vm();
}

PublicURLManager& ScriptExecutionContext::publicURLManager()
{
    if (!m_publicURLManager)
        m_publicURLManager = PublicURLManager::create(this);
    return *m_publicURLManager;
}

Ref<PublicURLManager> ScriptExecutionContext::protectedPublicURLManager()
{
    return publicURLManager();
}

void ScriptExecutionContext::adjustMinimumDOMTimerInterval(Seconds oldMinimumTimerInterval)
{
    if (minimumDOMTimerInterval() != oldMinimumTimerInterval) {
        for (auto& timer : m_timeouts.values())
            timer->updateTimerIntervalIfNecessary();
    }
}

Seconds ScriptExecutionContext::minimumDOMTimerInterval() const
{
    // The default implementation returns the DOMTimer's default
    // minimum timer interval. FIXME: to make it work with dedicated
    // workers, we will have to override it in the appropriate
    // subclass, and provide a way to enumerate a Document's dedicated
    // workers so we can update them all.
    return DOMTimer::defaultMinimumInterval();
}

void ScriptExecutionContext::didChangeTimerAlignmentInterval()
{
    CheckedRef eventLoop = this->eventLoop();
    for (auto& timer : m_timeouts.values())
        eventLoop->didChangeTimerAlignmentInterval(timer->timer());
}

Seconds ScriptExecutionContext::domTimerAlignmentInterval(bool) const
{
    return DOMTimer::defaultAlignmentInterval();
}

RejectedPromiseTracker* ScriptExecutionContext::ensureRejectedPromiseTrackerSlow()
{
    // ScriptExecutionContext::vm() in Worker is only available after WorkerGlobalScope initialization is done.
    // When initializing ScriptExecutionContext, vm() is not ready.

    ASSERT(!m_rejectedPromiseTracker);
    if (auto* globalScope = dynamicDowncast<WorkerOrWorkletGlobalScope>(*this)) {
        CheckedPtr scriptController = globalScope->script();
        // Do not re-create the promise tracker if we are in a worker / worklet whose execution is terminating.
        if (!scriptController || scriptController->isTerminatingExecution())
            return nullptr;
    }
    m_rejectedPromiseTracker = makeUnique<RejectedPromiseTracker>(*this, protectedVM());
    return m_rejectedPromiseTracker.get();
}

void ScriptExecutionContext::removeRejectedPromiseTracker()
{
    m_rejectedPromiseTracker = nullptr;
}

void ScriptExecutionContext::setDatabaseContext(DatabaseContext* databaseContext)
{
    m_databaseContext = databaseContext;
}

bool ScriptExecutionContext::hasPendingActivity() const
{
    checkConsistency();

    // This runs on the GC thread.
    for (SUPPRESS_UNCOUNTED_LOCAL auto* activeDOMObject : m_activeDOMObjects) {
        if (activeDOMObject->hasPendingActivity())
            return true;
    }

    return false;
}

JSC::JSGlobalObject* ScriptExecutionContext::globalObject() const
{
    if (auto* document = dynamicDowncast<Document>(*this)) {
        RefPtr frame = document->frame();
        return frame ? frame->checkedScript()->globalObject(mainThreadNormalWorldSingleton()) : nullptr;
    }

    if (auto* globalScope = dynamicDowncast<WorkerOrWorkletGlobalScope>(*this)) {
        CheckedPtr script = globalScope->script();
        return script ? script->globalScopeWrapper() : nullptr;
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

String ScriptExecutionContext::domainForCachePartition() const
{
    if (!m_domainForCachePartition.isNull())
        return m_domainForCachePartition;

    if (m_storageBlockingPolicy != StorageBlockingPolicy::BlockThirdParty)
        return emptyString();

    return protectedTopOrigin()->domainForCachePartition();
}

bool ScriptExecutionContext::allowsMediaDevices() const
{
#if ENABLE(MEDIA_STREAM)
    auto* document = dynamicDowncast<Document>(*this);
    if (!document)
        return false;
    RefPtr page = document->page();
    return page ? !page->settings().mediaCaptureRequiresSecureConnection() : false;
#else
    return false;
#endif
}

void ScriptExecutionContext::setActiveServiceWorker(RefPtr<ServiceWorker>&& serviceWorker)
{
    m_activeServiceWorker = WTFMove(serviceWorker);
}

void ScriptExecutionContext::registerServiceWorker(ServiceWorker& serviceWorker)
{
    auto addResult = m_serviceWorkers.add(serviceWorker.identifier(), &serviceWorker);
    ASSERT_UNUSED(addResult, addResult.isNewEntry);
}

void ScriptExecutionContext::unregisterServiceWorker(ServiceWorker& serviceWorker)
{
    m_serviceWorkers.remove(serviceWorker.identifier());
}

ServiceWorkerContainer* ScriptExecutionContext::serviceWorkerContainer()
{
    RefPtr<NavigatorBase> navigator;
    if (auto* document = dynamicDowncast<Document>(*this)) {
        if (RefPtr window = document->window())
            navigator = window->optionalNavigator();
    } else
        navigator = downcast<WorkerGlobalScope>(*this).optionalNavigator();

    return navigator ? &navigator->serviceWorker() : nullptr;
}

ServiceWorkerContainer* ScriptExecutionContext::ensureServiceWorkerContainer()
{
    RefPtr<NavigatorBase> navigator;
    if (auto* document = dynamicDowncast<Document>(*this)) {
        if (RefPtr window = document->window())
            navigator = &window->navigator();
    } else
        navigator = &downcast<WorkerGlobalScope>(*this).navigator();

    return navigator ? &navigator->serviceWorker() : nullptr;
}

void ScriptExecutionContext::setCrossOriginMode(CrossOriginMode crossOriginMode)
{
    globalCrossOriginMode = crossOriginMode;
    if (crossOriginMode == CrossOriginMode::Isolated)
        Performance::allowHighPrecisionTime();
}

CrossOriginMode ScriptExecutionContext::crossOriginMode()
{
    return globalCrossOriginMode;
}

bool ScriptExecutionContext::postTaskTo(ScriptExecutionContextIdentifier identifier, Task&& task)
{
    Locker locker { allScriptExecutionContextsMapLock };
    // Called from non-main thread.
    SUPPRESS_UNCOUNTED_LOCAL auto* context = allScriptExecutionContextsMap().get(identifier);

    if (!context)
        return false;

    context->postTask(WTFMove(task));
    return true;
}

bool ScriptExecutionContext::postTaskForModeToWorkerOrWorklet(ScriptExecutionContextIdentifier identifier, Task&& task, const String& mode)
{
    Locker locker { allScriptExecutionContextsMapLock };
    // Called from non-main thread.
    SUPPRESS_UNCOUNTED_LOCAL auto* context = dynamicDowncast<WorkerOrWorkletGlobalScope>(allScriptExecutionContextsMap().get(identifier));

    if (!context)
        return false;

    context->postTaskForMode(WTFMove(task), mode);
    return true;
}

bool ScriptExecutionContext::isContextThread(ScriptExecutionContextIdentifier identifier)
{
    Locker locker { allScriptExecutionContextsMapLock };
    RefPtr context = allScriptExecutionContextsMap().get(identifier);
    return context && context->isContextThread();
}

bool ScriptExecutionContext::ensureOnContextThread(ScriptExecutionContextIdentifier identifier, Task&& task)
{
    // Called from non-main thread.
    SUPPRESS_UNCOUNTED_LOCAL ScriptExecutionContext* context = nullptr;
    {
        Locker locker { allScriptExecutionContextsMapLock };
        context = allScriptExecutionContextsMap().get(identifier);

        if (!context)
            return false;

        if (!context->isContextThread()) {
            context->postTask(WTFMove(task));
            return true;
        }
    }

    task.performTask(*context);
    return true;
}

bool ScriptExecutionContext::ensureOnContextThreadForCrossThreadTask(ScriptExecutionContextIdentifier identifier, CrossThreadTask&& crossThreadTask)
{
    {
        Locker locker { allScriptExecutionContextsMapLock };
        // Called from non-main thread.
        SUPPRESS_UNCOUNTED_LOCAL auto* context = allScriptExecutionContextsMap().get(identifier);

        if (!context)
            return false;

        if (!context->isContextThread()) {
            context->postTask([crossThreadTask = WTFMove(crossThreadTask)](ScriptExecutionContext&) mutable {
                crossThreadTask.performTask();
            });
            return true;
        }
    }

    crossThreadTask.performTask();
    return true;
}

void ScriptExecutionContext::postTaskToResponsibleDocument(Function<void(Document&)>&& callback)
{
    if (auto* document = dynamicDowncast<Document>(*this)) {
        callback(*document);
        return;
    }

    auto* workerOrWorketGlobalScope = dynamicDowncast<WorkerOrWorkletGlobalScope>(*this);
    ASSERT(workerOrWorketGlobalScope);
    if (!workerOrWorketGlobalScope)
        return;

    if (RefPtr thread = workerOrWorketGlobalScope->workerOrWorkletThread()) {
        if (CheckedPtr workerLoaderProxy = thread->workerLoaderProxy()) {
            workerLoaderProxy->postTaskToLoader([callback = WTFMove(callback)](auto&& context) {
                callback(downcast<Document>(context));
            });
        }
        return;
    }

    if (RefPtr document = downcast<WorkletGlobalScope>(*this).responsibleDocument())
        callback(document.releaseNonNull());
}

static bool isOriginEquivalentToLocal(const SecurityOrigin& origin)
{
    return origin.isLocal() && !origin.needsStorageAccessFromFileURLsQuirk() && !origin.hasUniversalAccess();
}

ScriptExecutionContext::HasResourceAccess ScriptExecutionContext::canAccessResource(ResourceType type) const
{
    RefPtr origin = securityOrigin();
    if (!origin || origin->isOpaque())
        return HasResourceAccess::No;

    switch (type) {
    case ResourceType::Cookies:
    case ResourceType::Geolocation:
        return HasResourceAccess::Yes;
    case ResourceType::ApplicationCache:
    case ResourceType::Plugin:
    case ResourceType::WebSQL:
    case ResourceType::IndexedDB:
    case ResourceType::LocalStorage:
    case ResourceType::StorageManager:
        if (isOriginEquivalentToLocal(*origin))
            return HasResourceAccess::No;
        [[fallthrough]];
    case ResourceType::SessionStorage:
        if (m_storageBlockingPolicy == StorageBlockingPolicy::BlockAll)
            return HasResourceAccess::No;
        if ((m_storageBlockingPolicy == StorageBlockingPolicy::BlockThirdParty) && !protectedTopOrigin()->isSameOriginAs(*origin) && !origin->hasUniversalAccess())
            return HasResourceAccess::DefaultForThirdParty;
        return HasResourceAccess::Yes;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

ScriptExecutionContext::NotificationCallbackIdentifier ScriptExecutionContext::addNotificationCallback(CompletionHandler<void()>&& callback)
{
    auto identifier = NotificationCallbackIdentifier::generate();
    m_notificationCallbacks.add(identifier, WTFMove(callback));
    return identifier;
}

CompletionHandler<void()> ScriptExecutionContext::takeNotificationCallback(NotificationCallbackIdentifier identifier)
{
    return m_notificationCallbacks.take(identifier);
}

void ScriptExecutionContext::ref()
{
    switch (m_type) {
    case Type::Document:
        uncheckedDowncast<Document>(*this).ref();
        break;
    case Type::WorkerOrWorkletGlobalScope:
        uncheckedDowncast<WorkerOrWorkletGlobalScope>(*this).ref();
        break;
    case Type::EmptyScriptExecutionContext:
        uncheckedDowncast<EmptyScriptExecutionContext>(*this).ref();
        break;
    }
}

void ScriptExecutionContext::deref()
{
    switch (m_type) {
    case Type::Document:
        uncheckedDowncast<Document>(*this).deref();
        break;
    case Type::WorkerOrWorkletGlobalScope:
        uncheckedDowncast<WorkerOrWorkletGlobalScope>(*this).deref();
        break;
    case Type::EmptyScriptExecutionContext:
        uncheckedDowncast<EmptyScriptExecutionContext>(*this).deref();
        break;
    }
}

// ScriptExecutionContextDispatcher is not guaranteeing dispatching on its own for workers.
// Together with ScriptExecutionContext::enqueueTaskWhenSettled, it meets NativePromise dispatcher contract.
// FIXME: We should investigate how to guarantee task dispatching to workers.
class ScriptExecutionContextDispatcher final
    : public GuaranteedSerialFunctionDispatcher {
public:
    static Ref<ScriptExecutionContextDispatcher> create(ScriptExecutionContext& context) { return adoptRef(*new ScriptExecutionContextDispatcher(context)); }

private:
    explicit ScriptExecutionContextDispatcher(ScriptExecutionContext& context)
        : m_identifier(context.identifier())
        , m_threadId(context.isWorkerGlobalScope() ? Thread::currentSingleton().uid() : 1)
    {
    }

    // GuaranteedSerialFunctionDispatcher
    void dispatch(Function<void()>&& callback) final
    {
        if (m_threadId == 1) {
            callOnMainThread(WTFMove(callback));
            return;
        }
        ScriptExecutionContext::postTaskTo(m_identifier, WTFMove(callback));
    }
    bool isCurrent() const final { return m_threadId == Thread::currentSingleton().uid(); }

    ScriptExecutionContextIdentifier m_identifier;
    const uint32_t m_threadId { 1 };
};

GuaranteedSerialFunctionDispatcher& ScriptExecutionContext::nativePromiseDispatcher()
{
    if (!m_nativePromiseDispatcher)
        m_nativePromiseDispatcher = ScriptExecutionContextDispatcher::create(*this);
    return *m_nativePromiseDispatcher;
}

bool ScriptExecutionContext::requiresScriptTrackingPrivacyProtection(ScriptTrackingPrivacyCategory category)
{
    RefPtr vm = vmIfExists();
    if (!vm)
        return false;

    if (!vm->topCallFrame)
        return false;

    if (!shouldEnableScriptTrackingPrivacy(category, advancedPrivacyProtections()))
        return false;

    auto [taintedness, taintedURL] = JSC::sourceTaintedOriginFromStack(*vm, vm->topCallFrame);
    switch (taintedness) {
    case JSC::SourceTaintedOrigin::Untainted:
    case JSC::SourceTaintedOrigin::IndirectlyTaintedByHistory:
        return false;
    case JSC::SourceTaintedOrigin::IndirectlyTainted:
    case JSC::SourceTaintedOrigin::KnownTainted:
        break;
    }

    RefPtr document = dynamicDowncast<Document>(*this);
    if (!document)
        return true;

    RefPtr page = document->page();
    if (!page)
        return true;

    if (page->shouldAllowScriptAccess(taintedURL, protectedTopOrigin(), category))
        return false;

    if (!page->settings().scriptTrackingPrivacyLoggingEnabled())
        return true;

    if (!page->reportScriptTrackingPrivacy(taintedURL, category))
        return true;

    addConsoleMessage(MessageSource::JS, MessageLevel::Info, makeLogMessage(taintedURL, category));
    return true;
}

bool ScriptExecutionContext::isAlwaysOnLoggingAllowed() const
{
    auto sessionID = this->sessionID();
    if (!sessionID)
        return false;

    return sessionID->isAlwaysOnLoggingAllowed() || settingsValues().allowPrivacySensitiveOperationsInNonPersistentDataStores;
}

WebCoreOpaqueRoot root(ScriptExecutionContext* context)
{
    return WebCoreOpaqueRoot { context };
}

} // namespace WebCore
