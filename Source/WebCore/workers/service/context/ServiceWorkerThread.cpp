/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
#include "ServiceWorkerThread.h"

#include "AdvancedPrivacyProtections.h"
#include "BackgroundFetchManager.h"
#include "BackgroundFetchUpdateUIEvent.h"
#include "CacheStorageProvider.h"
#include "CommonAtomStrings.h"
#include "ContentSecurityPolicyResponseHeaders.h"
#include "EventLoop.h"
#include "EventNames.h"
#include "ExtendableMessageEvent.h"
#include "InstallEvent.h"
#include "JSDOMPromise.h"
#include "LoaderStrategy.h"
#include "Logging.h"
#include "NotificationData.h"
#include "NotificationEvent.h"
#include "NotificationPayload.h"
#include "PlatformStrategies.h"
#include "PushEvent.h"
#include "PushSubscription.h"
#include "PushSubscriptionChangeEvent.h"
#include "SWContextManager.h"
#include "SecurityOrigin.h"
#include "ServiceWorkerFetch.h"
#include "ServiceWorkerGlobalScope.h"
#include "ServiceWorkerRegistrationBackgroundFetchAPI.h"
#include "ServiceWorkerWindowClient.h"
#include "WorkerDebuggerProxy.h"
#include "WorkerLoaderProxy.h"
#include "WorkerObjectProxy.h"
#include <JavaScriptCore/IdentifiersFactory.h>
#include <JavaScriptCore/RuntimeFlags.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/MakeString.h>

using namespace PAL;

namespace WebCore {

class DummyServiceWorkerThreadProxy final : public WorkerObjectProxy, public CanMakeThreadSafeCheckedPtr<DummyServiceWorkerThreadProxy> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DummyServiceWorkerThreadProxy);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(DummyServiceWorkerThreadProxy);
public:
    static DummyServiceWorkerThreadProxy& shared()
    {
        static NeverDestroyed<DummyServiceWorkerThreadProxy> proxy;
        return proxy;
    }

    // CanMakeCheckedPtr.
    uint32_t checkedPtrCount() const { return CanMakeThreadSafeCheckedPtr<DummyServiceWorkerThreadProxy>::checkedPtrCount(); }
    uint32_t checkedPtrCountWithoutThreadCheck() const { return CanMakeThreadSafeCheckedPtr<DummyServiceWorkerThreadProxy>::checkedPtrCountWithoutThreadCheck(); }
    void incrementCheckedPtrCount() const { CanMakeThreadSafeCheckedPtr<DummyServiceWorkerThreadProxy>::incrementCheckedPtrCount(); }
    void decrementCheckedPtrCount() const { CanMakeThreadSafeCheckedPtr<DummyServiceWorkerThreadProxy>::decrementCheckedPtrCount(); }

private:
    void postExceptionToWorkerObject(const String&, int, int, const String&) final { };
    void reportErrorToWorkerObject(const String&) final { };
    void workerGlobalScopeDestroyed() final { };
    void postMessageToWorkerObject(MessageWithMessagePorts&&) final { };
};

// FIXME: Use a valid WorkerReportingProxy
// FIXME: Use a valid WorkerObjectProxy
// FIXME: Use valid runtime flags

static WorkerParameters generateWorkerParameters(const ServiceWorkerContextData& contextData, String&& userAgent, WorkerThreadMode workerThreadMode, const SettingsValues& settingsValues, PAL::SessionID sessionID, OptionSet<AdvancedPrivacyProtections> advancedPrivacyProtections, std::optional<uint64_t> noiseInjectionHashSalt)
{
    return {
        contextData.scriptURL,
        URL(), // FIXME: Should pass owner URL.
        emptyString(),
        makeString("serviceworker:"_s, Inspector::IdentifiersFactory::createIdentifier()),
        WTFMove(userAgent),
        platformStrategies()->loaderStrategy()->isOnLine(),
        contextData.contentSecurityPolicy,
        false,
        contextData.crossOriginEmbedderPolicy,
        MonotonicTime::now(),
        { },
        contextData.workerType,
        FetchRequestCredentials::Omit,
        settingsValues,
        workerThreadMode,
        sessionID,
        { },
        { },
        advancedPrivacyProtections,
        noiseInjectionHashSalt
    };
}

static WorkerThreadStartMode threadStartModeFromSettings()
{
#if ENABLE(REMOTE_INSPECTOR) && ENABLE(REMOTE_INSPECTOR_SERVICE_WORKER_AUTO_INSPECTION)
    // FIXME: Find a reasonable heuristic for when the service worker definitely won't be
    // automatically inspected, in which case there's no need to force this start mode.
    return WorkerThreadStartMode::WaitForInspector;
#else
    return WorkerThreadStartMode::Normal;
#endif
}

ServiceWorkerThread::ServiceWorkerThread(ServiceWorkerContextData&& contextData, ServiceWorkerData&& workerData, String&& userAgent, WorkerThreadMode workerThreadMode, const SettingsValues& settingsValues, WorkerLoaderProxy& loaderProxy, WorkerDebuggerProxy& debuggerProxy, WorkerBadgeProxy& badgeProxy, IDBClient::IDBConnectionProxy* idbConnectionProxy, SocketProvider* socketProvider, std::unique_ptr<NotificationClient>&& notificationClient, PAL::SessionID sessionID, std::optional<uint64_t> noiseInjectionHashSalt, OptionSet<AdvancedPrivacyProtections> advancedPrivacyProtections)
    : WorkerThread(generateWorkerParameters(contextData, WTFMove(userAgent), workerThreadMode, settingsValues, sessionID, advancedPrivacyProtections, noiseInjectionHashSalt), contextData.script, loaderProxy, debuggerProxy, DummyServiceWorkerThreadProxy::shared(), badgeProxy, threadStartModeFromSettings(), contextData.registration.key.topOrigin().securityOrigin().get(), idbConnectionProxy, socketProvider, JSC::RuntimeFlags::createAllEnabled())
    , m_serviceWorkerIdentifier(contextData.serviceWorkerIdentifier)
    , m_jobDataIdentifier(contextData.jobDataIdentifier)
    , m_contextData(crossThreadCopy(WTFMove(contextData)))
    , m_workerData(crossThreadCopy(WTFMove(workerData)))
    , m_workerObjectProxy(DummyServiceWorkerThreadProxy::shared())
    , m_heartBeatTimeout(settingsValues.shouldUseServiceWorkerShortTimeout ? heartBeatTimeoutForTest : heartBeatTimeout)
    , m_heartBeatTimer { *this, &ServiceWorkerThread::heartBeatTimerFired }
    , m_notificationClient(WTFMove(notificationClient))
{
    ASSERT(isMainThread());
    initializeCommonAtomStrings();
}

ServiceWorkerThread::~ServiceWorkerThread() = default;

Ref<WorkerGlobalScope> ServiceWorkerThread::createWorkerGlobalScope(const WorkerParameters& params, Ref<SecurityOrigin>&& origin, Ref<SecurityOrigin>&& topOrigin)
{
    RELEASE_ASSERT(m_contextData);
    return ServiceWorkerGlobalScope::create(*std::exchange(m_contextData, std::nullopt), *std::exchange(m_workerData, std::nullopt), params, WTFMove(origin), *this, WTFMove(topOrigin), idbConnectionProxy(), socketProvider(), WTFMove(m_notificationClient), WTFMove(m_workerClient));
}

void ServiceWorkerThread::runEventLoop()
{
    // FIXME: There will be ServiceWorker specific things to do here.
    WorkerThread::runEventLoop();
}

void ServiceWorkerThread::queueTaskToFireFetchEvent(Ref<ServiceWorkerFetch::Client>&& client, ResourceRequest&& request, String&& referrer, FetchOptions&& options, SWServerConnectionIdentifier connectionIdentifier, FetchIdentifier fetchIdentifier, bool isServiceWorkerNavigationPreloadEnabled, String&& clientIdentifier, String&& resultingClientIdentifier)
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [serviceWorkerGlobalScope, client = WTFMove(client), request = WTFMove(request), referrer = WTFMove(referrer), options = WTFMove(options), connectionIdentifier, fetchIdentifier, isServiceWorkerNavigationPreloadEnabled, clientIdentifier = WTFMove(clientIdentifier), resultingClientIdentifier = WTFMove(resultingClientIdentifier)]() mutable {
        if (client->isCancelled()) {
            RELEASE_LOG_INFO(ServiceWorker, "Skipping fetch event dispatching since client cancelled it");
            return;
        }

        ServiceWorkerFetch::dispatchFetchEvent(WTFMove(client), serviceWorkerGlobalScope, WTFMove(request), WTFMove(referrer), WTFMove(options), connectionIdentifier, fetchIdentifier, isServiceWorkerNavigationPreloadEnabled, WTFMove(clientIdentifier), WTFMove(resultingClientIdentifier));
    });
}

static void fireMessageEvent(ServiceWorkerGlobalScope& scope, MessageWithMessagePorts&& message, ExtendableMessageEventSource&& source, const URL& sourceURL)
{
    auto* globalObject = scope.globalObject();
    if (!globalObject)
        return;

    auto ports = MessagePort::entanglePorts(scope, WTFMove(message.transferredPorts));
    auto messageEvent = ExtendableMessageEvent::create(*globalObject, WTFMove(ports), message.message.releaseNonNull(), SecurityOriginData::fromURL(sourceURL).toString(), { }, source);
    scope.dispatchEvent(messageEvent.event);
    scope.updateExtendedEventsSet(messageEvent.event.ptr());
}

void ServiceWorkerThread::queueTaskToPostMessage(MessageWithMessagePorts&& message, ServiceWorkerOrClientData&& sourceData)
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [weakThis = ThreadSafeWeakPtr { *this }, serviceWorkerGlobalScope, message = WTFMove(message), sourceData = WTFMove(sourceData)]() mutable {
        URL sourceURL;
        ExtendableMessageEventSource source;
        if (std::holds_alternative<ServiceWorkerClientData>(sourceData)) {
            RefPtr<ServiceWorkerClient> sourceClient = ServiceWorkerClient::create(serviceWorkerGlobalScope, WTFMove(std::get<ServiceWorkerClientData>(sourceData)));

            RELEASE_ASSERT(!sourceClient->url().protocolIsInHTTPFamily() || !serviceWorkerGlobalScope->url().protocolIsInHTTPFamily() || protocolHostAndPortAreEqual(serviceWorkerGlobalScope->url(), sourceClient->url()));

            sourceURL = sourceClient->url();
            source = WTFMove(sourceClient);
        } else {
            RefPtr<ServiceWorker> sourceWorker = ServiceWorker::getOrCreate(serviceWorkerGlobalScope, WTFMove(std::get<ServiceWorkerData>(sourceData)));

            RELEASE_ASSERT(!sourceWorker->scriptURL().protocolIsInHTTPFamily() || !serviceWorkerGlobalScope->url().protocolIsInHTTPFamily() || protocolHostAndPortAreEqual(serviceWorkerGlobalScope->url(), sourceWorker->scriptURL()));

            sourceURL = sourceWorker->scriptURL();
            source = WTFMove(sourceWorker);
        }
        fireMessageEvent(serviceWorkerGlobalScope, WTFMove(message), ExtendableMessageEventSource { source }, sourceURL);
        callOnMainThread([weakThis = WTFMove(weakThis)] {
            if (RefPtr protectedThis = weakThis.get())
                protectedThis->finishedFiringMessageEvent();
        });
    });
}

void ServiceWorkerThread::queueTaskToFireInstallEvent()
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [weakThis = ThreadSafeWeakPtr { *this }, serviceWorkerGlobalScope]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireInstallEvent firing event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        Ref installEvent = serviceWorkerGlobalScope->settingsValues().serviceWorkerInstallEventEnabled ? Ref<ExtendableEvent> { InstallEvent::create(eventNames().installEvent, { }, ExtendableEvent::IsTrusted::Yes) } : ExtendableEvent::create(eventNames().installEvent, { }, ExtendableEvent::IsTrusted::Yes);
        serviceWorkerGlobalScope->dispatchEvent(installEvent);

        installEvent->whenAllExtendLifetimePromisesAreSettled([weakThis = WTFMove(weakThis)](HashSet<Ref<DOMPromise>>&& extendLifetimePromises) mutable {
            bool hasRejectedAnyPromise = false;
            for (auto& promise : extendLifetimePromises) {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    hasRejectedAnyPromise = true;
                    break;
                }
            }
            callOnMainThread([weakThis = WTFMove(weakThis), hasRejectedAnyPromise] {
                RefPtr protectedThis = weakThis.get();
                RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireInstallEvent finishing for worker %" PRIu64, protectedThis ? protectedThis->identifier().toUInt64() : 0);
                if (protectedThis)
                    protectedThis->finishedFiringInstallEvent(hasRejectedAnyPromise);
            });
        });
    });
}

void ServiceWorkerThread::queueTaskToFireActivateEvent()
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [weakThis = ThreadSafeWeakPtr { *this }, serviceWorkerGlobalScope]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireActivateEvent firing event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        auto activateEvent = ExtendableEvent::create(eventNames().activateEvent, { }, ExtendableEvent::IsTrusted::Yes);
        serviceWorkerGlobalScope->dispatchEvent(activateEvent);

        activateEvent->whenAllExtendLifetimePromisesAreSettled([weakThis = WTFMove(weakThis)](auto&&) mutable {
            callOnMainThread([weakThis = WTFMove(weakThis)] {
                RefPtr protectedThis = weakThis.get();
                RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireActivateEvent finishing for worker %" PRIu64, protectedThis ? protectedThis->identifier().toUInt64() : 0);
                if (protectedThis)
                    protectedThis->finishedFiringActivateEvent();
            });
        });
    });
}

void ServiceWorkerThread::queueTaskToFirePushEvent(std::optional<Vector<uint8_t>>&& data, std::optional<NotificationPayload>&& payload, Function<void(bool, std::optional<NotificationPayload>&&)>&& callback)
{
#if ENABLE(DECLARATIVE_WEB_PUSH)
    // Logic for pushnotification events is different enough to not share this same implementation body.
    if (payload) {
        queueTaskToFireDeclarativePushEvent(WTFMove(*payload), WTFMove(callback));
        return;
    }
#else
    UNUSED_PARAM(payload);
#endif

    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [serviceWorkerGlobalScope, data = WTFMove(data), callback = WTFMove(callback)]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFirePushEvent firing event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        serviceWorkerGlobalScope->setHasPendingSilentPushEvent(true);

        auto pushEvent = PushEvent::create(eventNames().pushEvent, { }, WTFMove(data), ExtendableEvent::IsTrusted::Yes);
        serviceWorkerGlobalScope->dispatchPushEvent(pushEvent);

        pushEvent->whenAllExtendLifetimePromisesAreSettled([serviceWorkerGlobalScope, eventCreationTime = pushEvent->timeStamp(), callback = WTFMove(callback)](auto&& extendLifetimePromises) mutable {
            bool hasRejectedAnyPromise = false;
            for (auto& promise : extendLifetimePromises) {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    hasRejectedAnyPromise = true;
                    break;
                }
            }

            bool showedNotification = !serviceWorkerGlobalScope->hasPendingSilentPushEvent();
            serviceWorkerGlobalScope->setHasPendingSilentPushEvent(false);
            if (!showedNotification)
                serviceWorkerGlobalScope->addConsoleMessage(MessageSource::Storage, MessageLevel::Error, "Push event handling completed without showing any notification via ServiceWorkerRegistration.showNotification(). This may trigger removal of the push subscription."_s, 0);
            bool success = !hasRejectedAnyPromise && showedNotification;

            RELEASE_LOG_ERROR_IF(!success, ServiceWorker, "ServiceWorkerThread::queueTaskToFirePushEvent failed to process push event (rejectedPromise = %d, showedNotification = %d)", hasRejectedAnyPromise, showedNotification);

            callback(success, std::nullopt);
        });
    });
}

#if ENABLE(DECLARATIVE_WEB_PUSH)
void ServiceWorkerThread::queueTaskToFireDeclarativePushEvent(NotificationPayload&& proposedPayload, Function<void(bool, std::optional<NotificationPayload>&&)>&& callback)
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    auto scopeURL = serviceWorkerGlobalScope->registration().data().scopeURL;
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [weakThis = ThreadSafeWeakPtr { *this }, serviceWorkerGlobalScope = Ref { serviceWorkerGlobalScope }, proposedPayload = WTFMove(proposedPayload), callback = WTFMove(callback), scopeURL]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireDeclarativePushEvent firing pushnotification event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        auto notification = Notification::create(serviceWorkerGlobalScope.get(), scopeURL, proposedPayload);
        Ref declarativePushEvent = PushEvent::create(eventNames().pushEvent, { }, notification.get(), proposedPayload.appBadge, ExtendableEvent::IsTrusted::Yes);
        serviceWorkerGlobalScope->dispatchDeclarativePushEvent(declarativePushEvent);

        declarativePushEvent->whenAllExtendLifetimePromisesAreSettled([serviceWorkerGlobalScope = Ref { serviceWorkerGlobalScope }, proposedPayload = WTFMove(proposedPayload), declarativePushEvent = WTFMove(declarativePushEvent), callback = WTFMove(callback)](auto&& extendLifetimePromises) mutable {
            serviceWorkerGlobalScope->clearDeclarativePushEvent();

            bool hasRejectedAnyPromise = false;
            for (auto& promise : extendLifetimePromises) {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    hasRejectedAnyPromise = true;
                    break;
                }
            }

            bool showedNotification = !serviceWorkerGlobalScope->hasPendingSilentPushEvent();
            serviceWorkerGlobalScope->setHasPendingSilentPushEvent(false);
            if (!showedNotification)
                serviceWorkerGlobalScope->addConsoleMessage(MessageSource::Storage, MessageLevel::Error, "Push event ended without showing any notification may trigger removal of the push subscription."_s, 0);
            bool success = !hasRejectedAnyPromise && showedNotification;

            RELEASE_LOG_ERROR_IF(!success, ServiceWorker, "ServiceWorkerThread::queueTaskToFirePushEvent failed to process push event (rejectedPromise = %d, showedNotification = %d)", hasRejectedAnyPromise, showedNotification);

            auto proposedAppBadge = proposedPayload.appBadge;
            auto data = declarativePushEvent->updatedNotificationData();
            std::optional<NotificationPayload> resultPayload = data ? NotificationPayload::fromNotificationData(*data) : WTFMove(proposedPayload);
            RELEASE_ASSERT(resultPayload);

            if (auto updatedAppBadge = declarativePushEvent->updatedAppBadge())
                resultPayload->appBadge = *updatedAppBadge;
            else
                resultPayload->appBadge = proposedAppBadge;

            callback(true, WTFMove(*resultPayload));
        });
    });
}
#endif

void ServiceWorkerThread::queueTaskToFirePushSubscriptionChangeEvent(std::optional<PushSubscriptionData>&& newSubscriptionData, std::optional<PushSubscriptionData>&& oldSubscriptionData)
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [weakThis = ThreadSafeWeakPtr { *this }, serviceWorkerGlobalScope, newSubscriptionData = WTFMove(newSubscriptionData), oldSubscriptionData = WTFMove(oldSubscriptionData)]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFirePushSubscriptionChangeEvent firing event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        RefPtr<PushSubscription> newSubscription;
        RefPtr<PushSubscription> oldSubscription;

        if (newSubscriptionData)
            newSubscription = PushSubscription::create(WTFMove(*newSubscriptionData), &serviceWorkerGlobalScope->registration());
        if (oldSubscriptionData)
            oldSubscription = PushSubscription::create(WTFMove(*oldSubscriptionData));

        auto pushSubscriptionChangeEvent = PushSubscriptionChangeEvent::create(eventNames().pushsubscriptionchangeEvent, { }, WTFMove(newSubscription), WTFMove(oldSubscription), ExtendableEvent::IsTrusted::Yes);
        serviceWorkerGlobalScope->dispatchEvent(pushSubscriptionChangeEvent);

        pushSubscriptionChangeEvent->whenAllExtendLifetimePromisesAreSettled([weakThis = WTFMove(weakThis)](auto&&) mutable {
            callOnMainThread([weakThis = WTFMove(weakThis)] {
                RefPtr protectedThis = weakThis.get();
                RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFirePushSubscriptionChangeEvent finishing for worker %" PRIu64, protectedThis ? protectedThis->identifier().toUInt64() : 0);
                if (protectedThis)
                    protectedThis->finishedFiringPushSubscriptionChangeEvent();
            });
        });
    });
}

#if ENABLE(NOTIFICATION_EVENT)
void ServiceWorkerThread::queueTaskToFireNotificationEvent(NotificationData&& data, NotificationEventType eventType, Function<void(bool)>&& callback)
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [serviceWorkerGlobalScope, data = WTFMove(data), eventType, callback = WTFMove(callback)]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireNotificationEvent firing event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        auto notification = Notification::create(serviceWorkerGlobalScope.get(), WTFMove(data));
        AtomString eventName;
        switch (eventType) {
        case NotificationEventType::Click:
            eventName = eventNames().notificationclickEvent;
            notification->markAsShown();
            serviceWorkerGlobalScope->recordUserGesture();
            break;
        case NotificationEventType::Close:
            eventName = eventNames().notificationcloseEvent;
            break;
        }

        auto notificationEvent = NotificationEvent::create(eventName, notification.ptr(), emptyString(), ExtendableEvent::IsTrusted::Yes);
        serviceWorkerGlobalScope->dispatchEvent(notificationEvent);

        notificationEvent->whenAllExtendLifetimePromisesAreSettled([serviceWorkerGlobalScope, callback = WTFMove(callback)](auto&& extendLifetimePromises) mutable {
            bool success = true;
            for (auto& promise : extendLifetimePromises) {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    success = false;
                    break;
                }
            }

            RELEASE_LOG_ERROR_IF(!success, ServiceWorker, "ServiceWorkerThread::queueTaskToFireNotificationEvent failed to process notification event");
            callback(success);
        });
    });
}
#endif

void ServiceWorkerThread::queueTaskToFireBackgroundFetchEvent(BackgroundFetchInformation&& info, Function<void(bool)>&& callback)
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [weakThis = ThreadSafeWeakPtr { *this }, serviceWorkerGlobalScope, info = crossThreadCopy(WTFMove(info)), callback = WTFMove(callback)]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireBackgroundFetchEvent firing event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        Ref manager = ServiceWorkerRegistrationBackgroundFetchAPI::backgroundFetch(serviceWorkerGlobalScope->registration());
        BackgroundFetchEventInit eventInit { { }, manager->backgroundFetchRegistrationInstance(serviceWorkerGlobalScope, WTFMove(info)) };
        RefPtr<ExtendableEvent> event;
        switch (info.failureReason) {
        case BackgroundFetchFailureReason::EmptyString:
            event = BackgroundFetchUpdateUIEvent::create(eventNames().backgroundfetchsuccessEvent, WTFMove(eventInit), Event::IsTrusted::Yes);
            break;
        case BackgroundFetchFailureReason::Aborted:
            event = BackgroundFetchEvent::create(eventNames().backgroundfetchabortEvent, WTFMove(eventInit), Event::IsTrusted::Yes);
            break;
        default:
            event = BackgroundFetchEvent::create(eventNames().backgroundfetchfailEvent, WTFMove(eventInit), Event::IsTrusted::Yes);
            break;
        };

        serviceWorkerGlobalScope->dispatchEvent(*event);

        event->whenAllExtendLifetimePromisesAreSettled([serviceWorkerGlobalScope, callback = WTFMove(callback)](auto&& extendLifetimePromises) mutable {
            bool success = true;
            for (auto& promise : extendLifetimePromises) {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    success = false;
                    break;
                }
            }

            RELEASE_LOG_ERROR_IF(!success, ServiceWorker, "ServiceWorkerThread::queueTaskToFireBackgroundFetchEvent failed to process background fetch event");
            callback(success);
        });
    });
}

void ServiceWorkerThread::queueTaskToFireBackgroundFetchClickEvent(BackgroundFetchInformation&& info, Function<void(bool)>&& callback)
{
    Ref serviceWorkerGlobalScope = downcast<ServiceWorkerGlobalScope>(*globalScope());
    serviceWorkerGlobalScope->eventLoop().queueTask(TaskSource::DOMManipulation, [serviceWorkerGlobalScope, info = crossThreadCopy(WTFMove(info)), callback = WTFMove(callback)]() mutable {
        RELEASE_LOG(ServiceWorker, "ServiceWorkerThread::queueTaskToFireBackgroundFetchClickEvent firing event for worker %" PRIu64, serviceWorkerGlobalScope->thread().identifier().toUInt64());

        Ref manager = ServiceWorkerRegistrationBackgroundFetchAPI::backgroundFetch(serviceWorkerGlobalScope->registration());
        BackgroundFetchEventInit eventInit { { }, manager->backgroundFetchRegistrationInstance(serviceWorkerGlobalScope, WTFMove(info)) };
        auto event = BackgroundFetchEvent::create(eventNames().backgroundfetchclickEvent, WTFMove(eventInit));

        serviceWorkerGlobalScope->dispatchEvent(event.get());

        event->whenAllExtendLifetimePromisesAreSettled([serviceWorkerGlobalScope, callback = WTFMove(callback)](auto&& extendLifetimePromises) mutable {
            bool success = true;
            for (auto& promise : extendLifetimePromises) {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    success = false;
                    break;
                }
            }

            RELEASE_LOG_ERROR_IF(!success, ServiceWorker, "ServiceWorkerThread::queueTaskToFireBackgroundFetchClickEvent failed to process background fetch event");
            callback(success);
        });
    });
}

void ServiceWorkerThread::finishedEvaluatingScript()
{
    ASSERT(globalScope()->isContextThread());

    Ref scope = *dynamicDowncast<ServiceWorkerGlobalScope>(globalScope());

    scope->storeEventTypesToHandle();
    m_doesHandleFetch = scope->hasFetchEventHandler();
}

WorkerObjectProxy& ServiceWorkerThread::workerObjectProxy() const
{
    return m_workerObjectProxy.get();
}

void ServiceWorkerThread::start(Function<void(const String&, bool)>&& callback)
{
    m_state = State::Starting;
    startHeartBeatTimer();

    WorkerThread::start([callback = WTFMove(callback), weakThis = ThreadSafeWeakPtr { *this }](auto& errorMessage) mutable {
#ifndef NDEBUG
        if (!errorMessage.isEmpty())
            LOG(ServiceWorker, "Service worker thread failed to start: %s", errorMessage.utf8().data());
#endif
        bool doesHandleFetch = true;
        if (RefPtr protectedThis = weakThis.get()) {
            protectedThis->finishedStarting();
            doesHandleFetch = protectedThis->doesHandleFetch();
        }
        callback(errorMessage, doesHandleFetch);
    });
}

void ServiceWorkerThread::finishedStarting()
{
    m_state = State::Idle;
}

void ServiceWorkerThread::startFetchEventMonitoring()
{
    m_isHandlingFetchEvent = true;
    startHeartBeatTimer();
}

void ServiceWorkerThread::startFunctionalEventMonitoring()
{
    m_isHandlingFunctionalEvent = true;
    startHeartBeatTimer();
}

void ServiceWorkerThread::startNotificationPayloadFunctionalEventMonitoring()
{
    m_isHandlingNotificationPayloadFunctionalEvent = true;
    startHeartBeatTimer();
}

void ServiceWorkerThread::startHeartBeatTimer()
{
    // We cannot detect responsiveness for service workers running on the main thread by using a main thread timer.
    if (is<WorkerMainRunLoop>(runLoop()))
        return;

    if (m_heartBeatTimer.isActive())
        return;

    m_ongoingHeartBeatCheck = true;
    runLoop().postTask([this, protectedThis = Ref { *this }](auto&) mutable {
        callOnMainThread([this, protectedThis = WTFMove(protectedThis)]() {
            m_ongoingHeartBeatCheck = false;
        });
    });

    m_heartBeatTimer.startOneShot(m_heartBeatTimeout);
}

void ServiceWorkerThread::heartBeatTimerFired()
{
    if (!m_ongoingHeartBeatCheck) {
        if (m_state == State::Installing || m_state == State::Activating || m_isHandlingFetchEvent || m_isHandlingFunctionalEvent || m_isHandlingNotificationPayloadFunctionalEvent || m_pushSubscriptionChangeEventCount || m_messageEventCount)
            startHeartBeatTimer();
        return;
    }

    RefPtr serviceWorkerThreadProxy = SWContextManager::singleton().serviceWorkerThreadProxy(identifier());
    if (!serviceWorkerThreadProxy || serviceWorkerThreadProxy->isTerminatingOrTerminated())
        return;

    RefPtr connection = SWContextManager::singleton().connection();
    if (!connection)
        return;

    switch (m_state) {
    case State::Idle:
    case State::Activating:
        connection->didFailHeartBeatCheck(identifier());
        break;
    case State::Starting:
        connection->serviceWorkerFailedToStart(m_jobDataIdentifier, identifier(), "Service Worker script execution timed out"_s);
        break;
    case State::Installing:
        connection->didFinishInstall(m_jobDataIdentifier, identifier(), false);
        break;
    }
}

void ServiceWorkerThread::willPostTaskToFireInstallEvent()
{
    m_state = State::Installing;
    startHeartBeatTimer();
}

void ServiceWorkerThread::finishedFiringInstallEvent(bool hasRejectedAnyPromise)
{
    m_state = State::Idle;

    if (RefPtr connection = SWContextManager::singleton().connection())
        connection->didFinishInstall(m_jobDataIdentifier, identifier(), !hasRejectedAnyPromise);
}

void ServiceWorkerThread::willPostTaskToFireActivateEvent()
{
    m_state = State::Activating;
    startHeartBeatTimer();
}

void ServiceWorkerThread::finishedFiringActivateEvent()
{
    m_state = State::Idle;

    if (RefPtr connection = SWContextManager::singleton().connection())
        connection->didFinishActivation(identifier());
}

void ServiceWorkerThread::willPostTaskToFireMessageEvent()
{
    if (!m_messageEventCount++)
        startHeartBeatTimer();
}

void ServiceWorkerThread::finishedFiringMessageEvent()
{
    ASSERT(m_messageEventCount);
    --m_messageEventCount;
}

void ServiceWorkerThread::willPostTaskToFirePushSubscriptionChangeEvent()
{
    if (!m_pushSubscriptionChangeEventCount++)
        startHeartBeatTimer();
}

void ServiceWorkerThread::finishedFiringPushSubscriptionChangeEvent()
{
    ASSERT(m_pushSubscriptionChangeEventCount);
    --m_pushSubscriptionChangeEventCount;
}

} // namespace WebCore
