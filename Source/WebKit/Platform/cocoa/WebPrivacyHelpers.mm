/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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
#import "WebPrivacyHelpers.h"

#if ENABLE(ADVANCED_PRIVACY_PROTECTIONS)

#import "Logging.h"
#import "RestrictedOpenerType.h"
#import "WKContentRuleListStore.h"
#import <WebCore/DNS.h>
#import <WebCore/LinkDecorationFilteringData.h>
#import <WebCore/OrganizationStorageAccessPromptQuirk.h>
#import <numeric>
#import <pal/spi/cf/CFNetworkSPI.h>
#import <pal/spi/cocoa/NetworkSPI.h>
#import <time.h>
#import <wtf/BlockPtr.h>
#import <wtf/NeverDestroyed.h>
#import <wtf/RobinHoodHashMap.h>
#import <wtf/RunLoop.h>
#import <wtf/Scope.h>
#import <wtf/WeakRandom.h>
#import <wtf/cocoa/VectorCocoa.h>
#import <wtf/posix/SocketPOSIX.h>
#import <wtf/text/MakeString.h>
#import <pal/cocoa/WebPrivacySoftLink.h>

#if HAVE(SYSTEM_SUPPORT_FOR_ADVANCED_PRIVACY_PROTECTIONS)
SOFT_LINK_LIBRARY_OPTIONAL(libnetwork)
SOFT_LINK_OPTIONAL(libnetwork, nw_context_set_tracker_lookup_callback, void, __cdecl, (nw_context_t, nw_context_tracker_lookup_callback_t))
#endif

@interface WKWebPrivacyNotificationListener : NSObject

@end

@implementation WKWebPrivacyNotificationListener {
    BlockPtr<void()> _callback;
    WPResourceType _resourceType;
}

- (instancetype)initWithType:(WPResourceType)resourceType callback:(void(^)())callback
{
    if (!(self = [super init]))
        return nil;

    _resourceType = resourceType;
    _callback = callback;

    if (PAL::isWebPrivacyFrameworkAvailable())
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didUpdate:) name:PAL::get_WebPrivacy_WPResourceDataChangedNotificationName() object:nil];
    return self;
}

- (void)dealloc
{
    if (PAL::isWebPrivacyFrameworkAvailable())
        [[NSNotificationCenter defaultCenter] removeObserver:self name:PAL::get_WebPrivacy_WPResourceDataChangedNotificationName() object:nil];
    [super dealloc];
}

- (void)didUpdate:(NSNotification *)notification
{
    ASSERT(PAL::isWebPrivacyFrameworkAvailable());
    auto type = dynamic_objc_cast<NSNumber>([notification.userInfo objectForKey:PAL::get_WebPrivacy_WPNotificationUserInfoResourceTypeKey()]);
    if (!type)
        return;

    if (_callback && type.integerValue == _resourceType)
        _callback();
}

@end

namespace WebKit {

Ref<ListDataObserver> ListDataControllerBase::observeUpdates(Function<void()>&& callback)
{
    ASSERT(RunLoop::isMain());
    if (!m_notificationListener) {
        m_notificationListener = adoptNS([[WKWebPrivacyNotificationListener alloc] initWithType:static_cast<WPResourceType>(resourceTypeValue()) callback:^{
            updateList([weakThis = WeakPtr { *this }] {
                RefPtr protectedThis = weakThis.get();
                if (!protectedThis)
                    return;
                protectedThis->m_observers.forEach([](auto& observer) {
                    observer.invokeCallback();
                });
            });
        }]);
    }
    Ref observer = ListDataObserver::create(WTFMove(callback));
    m_observers.add(observer.get());
    return observer;
}

void ListDataControllerBase::initializeIfNeeded()
{
    if (hasCachedListData())
        return;

    if (std::exchange(m_wasInitialized, true))
        return;

    updateList([weakThis = WeakPtr { *this }] {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;
        protectedThis->m_observers.forEach([](auto& observer) {
            observer.invokeCallback();
        });
    });
}

unsigned LinkDecorationFilteringController::resourceTypeValue() const
{
    return WPResourceTypeLinkFilteringData;
}

void LinkDecorationFilteringController::updateList(CompletionHandler<void()>&& completionHandler)
{
    ASSERT(RunLoop::isMain());
    if (!PAL::isWebPrivacyFrameworkAvailable()) {
        completionHandler();
        return;
    }

    static NeverDestroyed<Vector<CompletionHandler<void()>, 1>> lookupCompletionHandlers;
    lookupCompletionHandlers->append(WTFMove(completionHandler));
    if (lookupCompletionHandlers->size() > 1)
        return;

    RetainPtr options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
    [options setAfterUpdates:NO];

    [[PAL::getWPResourcesClass() sharedInstance] requestLinkFilteringData:options.get() completionHandler:^(WPLinkFilteringData *data, NSError *error) {
        Vector<WebCore::LinkDecorationFilteringData> result;
        if (error)
            RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request query parameters from WebPrivacy.");
        else {
            auto rules = [data rules];
            for (WPLinkFilteringRule *rule : rules) {
                auto domain = WebCore::RegistrableDomain { URL { makeString("http://"_s, String { rule.domain }) } };
                result.append(WebCore::LinkDecorationFilteringData { WTFMove(domain), [rule respondsToSelector:@selector(path)] ? rule.path : @"", rule.queryParameter });
            }
            setCachedListData(WTFMove(result));
        }

        for (auto& completionHandler : std::exchange(lookupCompletionHandlers.get(), { }))
            completionHandler();
    }];

}

using LinkFilteringRulesCallback = CompletionHandler<void(Vector<WebCore::LinkDecorationFilteringData>&&)>;
void requestLinkDecorationFilteringData(LinkFilteringRulesCallback&& callback)
{
    if (!PAL::isWebPrivacyFrameworkAvailable()) {
        callback({ });
        return;
    }

    static BOOL canRequestAllowedQueryParameters = [] {
        return [PAL::getWPResourcesClass() instancesRespondToSelector:@selector(requestAllowedLinkFilteringData:completionHandler:)];
    }();

    if (!canRequestAllowedQueryParameters) {
        callback({ });
        return;
    }

    static NeverDestroyed<Vector<LinkFilteringRulesCallback, 1>> lookupCallbacks;
    lookupCallbacks->append(WTFMove(callback));
    if (lookupCallbacks->size() > 1)
        return;

    auto options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
    [options setAfterUpdates:NO];

    [[PAL::getWPResourcesClass() sharedInstance] requestAllowedLinkFilteringData:options.get() completionHandler:^(WPLinkFilteringData *data, NSError *error) {
        Vector<WebCore::LinkDecorationFilteringData> result;
        if (error)
            RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request allowed query parameters from WebPrivacy.");
        else {
            auto rules = [data rules];
            for (WPLinkFilteringRule *rule : rules) {
                auto domain = WebCore::RegistrableDomain { URL { makeString("http://"_s, String { rule.domain }) } };
                result.append(WebCore::LinkDecorationFilteringData { WTFMove(domain), { }, rule.queryParameter });
            }
        }

        auto callbacks = std::exchange(lookupCallbacks.get(), { });
        for (int i = callbacks.size() - 1; i >= 0; --i) {
            auto& callback = callbacks[i];
            if (i)
                callback(Vector { result });
            else
                callback(WTFMove(result));
        }
    }];
}

unsigned StorageAccessPromptQuirkController::resourceTypeValue() const
{
    return WPResourceTypeStorageAccessPromptQuirksData;
}

void StorageAccessPromptQuirkController::didUpdateCachedListData()
{
    m_cachedListData.shrinkToFit();
    RELEASE_LOG(ResourceLoadStatistics, "StorageAccessPromptQuirkController::didUpdateCachedListData: Loaded %lu storage access prompt(s) quirks from WebPrivacy.", m_cachedListData.size());
}

static HashMap<WebCore::RegistrableDomain, Vector<WebCore::RegistrableDomain>> quirkDomainsDictToMap(NSDictionary<NSString *, NSArray<NSString *> *> *quirkDomains)
{
    HashMap<WebCore::RegistrableDomain, Vector<WebCore::RegistrableDomain>> map;
    auto* topDomains = quirkDomains.allKeys;
    for (NSString *topDomain : topDomains) {
        Vector<WebCore::RegistrableDomain> subFrameDomains;
        for (NSString *subFrameDomain : [quirkDomains objectForKey:topDomain])
            subFrameDomains.append(WebCore::RegistrableDomain::fromRawString(subFrameDomain));
        map.add(WebCore::RegistrableDomain::fromRawString(String { topDomain }), WTFMove(subFrameDomains));
    }
    return map;
}

static Vector<URL> quirkPagesArrayToVector(NSArray<NSString *> *triggerPages)
{
    Vector<URL> triggers;
    for (NSString *page : triggerPages) {
        if (![page isEqualToString:@"*"])
            triggers.append(URL { page });
    }
    return triggers;
}

void StorageAccessPromptQuirkController::updateList(CompletionHandler<void()>&& completionHandler)
{
    ASSERT(RunLoop::isMain());
    if (!PAL::isWebPrivacyFrameworkAvailable() || ![PAL::getWPResourcesClass() instancesRespondToSelector:@selector(requestStorageAccessPromptQuirksData:completionHandler:)]) {
        RunLoop::mainSingleton().dispatch(WTFMove(completionHandler));
        return;
    }

    static MainRunLoopNeverDestroyed<Vector<CompletionHandler<void()>, 1>> lookupCompletionHandlers;
    lookupCompletionHandlers->append(WTFMove(completionHandler));
    if (lookupCompletionHandlers->size() > 1)
        return;

    RetainPtr options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
    [options setAfterUpdates:NO];

    [[PAL::getWPResourcesClass() sharedInstance] requestStorageAccessPromptQuirksData:options.get() completionHandler:^(WPStorageAccessPromptQuirksData *data, NSError *error) {
        Vector<WebCore::OrganizationStorageAccessPromptQuirk> result;
        if (error)
            RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request storage access quirks from WebPrivacy.");
        else {
            auto quirks = [data quirks];
            auto hasQuirkDomainsAndTriggerPages = [PAL::getWPStorageAccessPromptQuirkClass() instancesRespondToSelector:@selector(quirkDomains)] && [PAL::getWPStorageAccessPromptQuirkClass() instancesRespondToSelector:@selector(triggerPages)];
            for (WPStorageAccessPromptQuirk *quirk : quirks) {
                if (hasQuirkDomainsAndTriggerPages)
                    result.append(WebCore::OrganizationStorageAccessPromptQuirk { quirk.name, quirkDomainsDictToMap(quirk.quirkDomains), quirkPagesArrayToVector(quirk.triggerPages) });
                else
                    result.append(WebCore::OrganizationStorageAccessPromptQuirk { quirk.name, quirkDomainsDictToMap(quirk.domainPairings), { } });
            }
            setCachedListData(WTFMove(result));
        }

        for (auto& completionHandler : std::exchange(lookupCompletionHandlers.get(), { }))
            completionHandler();
    }];
}

unsigned StorageAccessUserAgentStringQuirkController::resourceTypeValue() const
{
    return WPResourceTypeStorageAccessUserAgentStringQuirksData;
}

void StorageAccessUserAgentStringQuirkController::updateList(CompletionHandler<void()>&& completionHandler)
{
    ASSERT(RunLoop::isMain());
    if (!PAL::isWebPrivacyFrameworkAvailable() || ![PAL::getWPResourcesClass() instancesRespondToSelector:@selector(requestStorageAccessUserAgentStringQuirksData:completionHandler:)]) {
        RunLoop::mainSingleton().dispatch(WTFMove(completionHandler));
        return;
    }

    static MainRunLoopNeverDestroyed<Vector<CompletionHandler<void()>, 1>> lookupCompletionHandlers;
    lookupCompletionHandlers->append(WTFMove(completionHandler));
    if (lookupCompletionHandlers->size() > 1)
        return;

    RetainPtr options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
    [options setAfterUpdates:NO];

    [[PAL::getWPResourcesClass() sharedInstance] requestStorageAccessUserAgentStringQuirksData:options.get() completionHandler:^(WPStorageAccessUserAgentStringQuirksData *data, NSError *error) {
        HashMap<WebCore::RegistrableDomain, String> result;
        if (error)
            RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request storage access user agent string quirks from WebPrivacy.");
        else {
            auto quirks = [data quirks];
            for (WPStorageAccessUserAgentStringQuirk *quirk : quirks)
                result.add(WebCore::RegistrableDomain::fromRawString(quirk.domain), quirk.userAgentString);
            setCachedListData(WTFMove(result));
        }

        for (auto& completionHandler : std::exchange(lookupCompletionHandlers.get(), { }))
            completionHandler();
    }];
}

RestrictedOpenerDomainsController& RestrictedOpenerDomainsController::shared()
{
    static MainRunLoopNeverDestroyed<RestrictedOpenerDomainsController> sharedInstance;
    return sharedInstance.get();
}

RestrictedOpenerDomainsController::RestrictedOpenerDomainsController()
{
    scheduleNextUpdate(ContinuousApproximateTime::now());
    update();

    m_notificationListener = adoptNS([[WKWebPrivacyNotificationListener alloc] initWithType:static_cast<WPResourceType>(WPResourceTypeRestrictedOpenerDomains) callback:^{
        update();
    }]);
}

static RestrictedOpenerType restrictedOpenerType(WPRestrictedOpenerType type)
{
    switch (type) {
    case WPRestrictedOpenerTypeNoOpener: return RestrictedOpenerType::NoOpener;
    case WPRestrictedOpenerTypePostMessageAndClose: return RestrictedOpenerType::PostMessageAndClose;
    default: return RestrictedOpenerType::Unrestricted;
    }
}

void RestrictedOpenerDomainsController::scheduleNextUpdate(ContinuousApproximateTime now)
{
    // Allow the list to be re-requested from the server sometime between [24, 26) hours from now.
    static WeakRandom random;
    m_nextScheduledUpdateTime = now + 24_h + random.get() * 2_h;
}

void RestrictedOpenerDomainsController::update()
{
    ASSERT(RunLoop::isMain());
    if (!PAL::isWebPrivacyFrameworkAvailable() || ![PAL::getWPResourcesClass() instancesRespondToSelector:@selector(requestRestrictedOpenerDomains:completionHandler:)])
        return;

    auto options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
    [options setAfterUpdates:NO];

    [[PAL::getWPResourcesClass() sharedInstance] requestRestrictedOpenerDomains:options.get() completionHandler:^(NSArray<WPRestrictedOpenerDomain *> *domains, NSError *error) {
        if (error) {
            RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request restricted opener domains from WebPrivacy");
            return;
        }

        HashMap<WebCore::RegistrableDomain, RestrictedOpenerType> restrictedOpenerTypes;
        restrictedOpenerTypes.reserveInitialCapacity(domains.count);

        for (WPRestrictedOpenerDomain *domainInfo in domains) {
            auto registrableDomain = WebCore::RegistrableDomain::fromRawString(domainInfo.domain);
            if (registrableDomain.isEmpty())
                continue;
            restrictedOpenerTypes.add(registrableDomain, restrictedOpenerType(domainInfo.openerType));
        }

        m_restrictedOpenerTypes = WTFMove(restrictedOpenerTypes);
    }];
}

RestrictedOpenerType RestrictedOpenerDomainsController::lookup(const WebCore::RegistrableDomain& domain) const
{
    auto now = ContinuousApproximateTime::now();
    if (now > m_nextScheduledUpdateTime) {
        auto mutableThis = const_cast<RestrictedOpenerDomainsController*>(this);
        mutableThis->scheduleNextUpdate(now);
        mutableThis->update();
    }

    auto it = m_restrictedOpenerTypes.find(domain);
    return it == m_restrictedOpenerTypes.end() ? RestrictedOpenerType::Unrestricted : it->value;
}

ResourceMonitorURLsController& ResourceMonitorURLsController::singleton()
{
    static MainRunLoopNeverDestroyed<ResourceMonitorURLsController> sharedInstance;
    return sharedInstance.get();
}

void ResourceMonitorURLsController::prepare(CompletionHandler<void(WKContentRuleList*, bool)>&& completionHandler)
{
    ASSERT(RunLoop::isMain());
    if (!PAL::isWebPrivacyFrameworkAvailable() || ![PAL::getWPResourcesClass() instancesRespondToSelector:@selector(prepareResourceMonitorRulesForStore:completionHandler:)]) {
        completionHandler(nullptr, false);
        return;
    }

    static MainRunLoopNeverDestroyed<Vector<CompletionHandler<void(WKContentRuleList*, bool)>, 1>> lookupCompletionHandlers;
    lookupCompletionHandlers->append(WTFMove(completionHandler));
    if (lookupCompletionHandlers->size() > 1)
        return;

    WKContentRuleListStore *store = [WKContentRuleListStore defaultStore];

    [[PAL::getWPResourcesClass() sharedInstance] prepareResourceMonitorRulesForStore:store completionHandler:^(WKContentRuleList *list, bool updated, NSError *error) {
        if (error)
            RELEASE_LOG_ERROR(ResourceMonitoring, "Failed to request resource monitor urls from WebPrivacy: %@", error);

        for (auto& completionHandler : std::exchange(lookupCompletionHandlers.get(), { }))
            completionHandler(list, updated);
    }];
}

void ResourceMonitorURLsController::getSource(CompletionHandler<void(String&&)>&& completionHandler)
{
    ASSERT(RunLoop::isMain());
    if (!PAL::isWebPrivacyFrameworkAvailable() || ![PAL::getWPResourcesClass() instancesRespondToSelector:@selector(requestResourceMonitorRulesSource:completionHandler:)]) {
        completionHandler({ });
        return;
    }

    static MainRunLoopNeverDestroyed<Vector<CompletionHandler<void(NSString *)>, 1>> lookupCompletionHandlers;
    lookupCompletionHandlers->append(WTFMove(completionHandler));
    if (lookupCompletionHandlers->size() > 1)
        return;

    [[PAL::getWPResourcesClass() sharedInstance] requestResourceMonitorRulesSource:nil completionHandler:^(NSString *source, NSError *error) {
        if (error)
            RELEASE_LOG_ERROR(ResourceMonitoring, "Failed to request resource monitor urls source from WebPrivacy");

        for (auto& completionHandler : std::exchange(lookupCompletionHandlers.get(), { }))
            completionHandler(source);
    }];
}

#if HAVE(SYSTEM_SUPPORT_FOR_ADVANCED_PRIVACY_PROTECTIONS)

inline static std::optional<WebCore::IPAddress> ipAddress(const struct sockaddr* address)
{
    if (auto* addressV4 = dynamicCastToIPV4SocketAddress(*address))
        return WebCore::IPAddress { addressV4->sin_addr };

    if (auto* addressV6 = dynamicCastToIPV6SocketAddress(*address))
        return WebCore::IPAddress { addressV6->sin6_addr };

    return std::nullopt;
}

inline static std::optional<WebCore::IPAddress> ipAddress(nw_endpoint_t endpoint)
{
    if (nw_endpoint_get_type(endpoint) != nw_endpoint_type_address)
        return std::nullopt;

    return ipAddress(nw_endpoint_get_address(endpoint));
}

inline static std::optional<const char*> hostname(nw_endpoint_t endpoint)
{
    if (nw_endpoint_get_type(endpoint) != nw_endpoint_type_host)
        return std::nullopt;

    return nw_endpoint_get_hostname(endpoint);
}

class TrackerAddressLookupInfo {
public:
    enum class CanBlock : bool { No, Yes };

    TrackerAddressLookupInfo(WebCore::IPAddress&& network, unsigned netMaskLength, String&& owner, String&& host, CanBlock canBlock)
        : m_network { WTFMove(network) }
        , m_netMaskLength { netMaskLength }
        , m_owner { owner.utf8() }
        , m_host { host.utf8() }
        , m_canBlock { canBlock }
    {
    }

    TrackerAddressLookupInfo(WPNetworkAddressRange *range)
        : m_network { ipAddress(range.address).value() }
        , m_netMaskLength { static_cast<unsigned>(range.netMaskLength) }
        , m_owner { range.owner.UTF8String }
        , m_host { range.host.UTF8String }
        , m_canBlock { CanBlock::Yes } // FIXME: Grab this from WPNetworkAddressRange as well, once it's available.
    {
    }

    TrackerAddressLookupInfo() = default;

    const CString& owner() const { return m_owner; }
    const CString& host() const { return m_host; }

    CanBlock canBlock() const { return m_canBlock; }

    static void populateIfNeeded()
    {
        static std::once_flag onceFlag;
        std::call_once(onceFlag, [&] {
            if (!PAL::isWebPrivacyFrameworkAvailable())
                return;

            auto options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
            [options setAfterUpdates:YES];

            [[PAL::getWPResourcesClass() sharedInstance] requestTrackerNetworkAddresses:options.get() completionHandler:^(NSArray<WPNetworkAddressRange *> *ranges, NSError *error) {
                if (error) {
                    RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request tracking IP addresses from WebPrivacy");
                    return;
                }

                version4List().clear();
                version6List().clear();

                for (WPNetworkAddressRange *range in ranges) {
                    switch (range.version) {
                    case WPNetworkAddressVersion4:
                        version4List().append({ range });
                        break;
                    case WPNetworkAddressVersion6:
                        version6List().append({ range });
                        break;
                    default:
                        RELEASE_LOG_ERROR(ResourceLoadStatistics, "Skipped invalid tracking IP address: %@", range);
                        break;
                    }
                }

                version4List().shrinkToFit();
                version6List().shrinkToFit();
            }];
        });
    }

    static const TrackerAddressLookupInfo* find(const WebCore::IPAddress& address)
    {
        auto& list = address.isIPv4() ? version4List() : version6List();
        if (list.isEmpty())
            return nullptr;

        size_t lower = 0;
        size_t upper = list.size() - 1;
        if (address < list[lower].m_network)
            upper = lower;
        else if (address > list[upper].m_network)
            lower = upper;
        else {
            while (upper - lower > 1) {
                auto middle = std::midpoint(lower, upper);
                auto compareResult = address <=> list[middle].m_network;
                if (is_eq(compareResult))
                    return &list[middle];
                if (is_lt(compareResult))
                    upper = middle;
                else if (is_gt(compareResult))
                    lower = middle;
                else {
                    ASSERT_NOT_REACHED();
                    return nullptr;
                }
            }
        }

        if (list[upper].contains(address))
            return &list[upper];

        if (upper != lower && list[lower].contains(address))
            return &list[lower];

        return nullptr;
    }

private:
    static Vector<TrackerAddressLookupInfo>& version4List()
    {
        static NeverDestroyed sharedList = [] {
            return Vector<TrackerAddressLookupInfo> { };
        }();
        return sharedList.get();
    }

    static Vector<TrackerAddressLookupInfo>& version6List()
    {
        static NeverDestroyed sharedList = [] {
            return Vector<TrackerAddressLookupInfo> { };
        }();
        return sharedList.get();
    }

    bool contains(const WebCore::IPAddress& address) const
    {
        return m_network.matchingNetMaskLength(address) >= m_netMaskLength;
    }

    WebCore::IPAddress m_network { WTF::HashTableEmptyValue };
    unsigned m_netMaskLength { 0 };
    CString m_owner;
    CString m_host;
    CanBlock m_canBlock { CanBlock::No };
};

class TrackerDomainLookupInfo {
public:
    enum class CanBlock : bool { No, Yes };

    TrackerDomainLookupInfo(String&& owner, CanBlock canBlock)
        : m_owner { owner.utf8() }
        , m_canBlock { canBlock }
    {
    }

    TrackerDomainLookupInfo(WPTrackingDomain *domain)
        : m_owner { domain.owner.UTF8String }
        , m_canBlock { domain.canBlock ? CanBlock::Yes : CanBlock::No }
    {
    }

    TrackerDomainLookupInfo() = default;

    const CString& owner() const { return m_owner; }

    CanBlock canBlock() const { return m_canBlock; }

    static void populateIfNeeded()
    {
        static std::once_flag onceFlag;
        std::call_once(onceFlag, [&] {
            if (!PAL::isWebPrivacyFrameworkAvailable())
                return;

            static BOOL canRequestTrackerDomainNames = [] {
                return [PAL::getWPResourcesClass() instancesRespondToSelector:@selector(requestTrackerDomainNamesData:completionHandler:)];
            }();

            if (!canRequestTrackerDomainNames)
                return;

            auto options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
            [options setAfterUpdates:YES];
            [[PAL::getWPResourcesClass() sharedInstance] requestTrackerDomainNamesData:options.get() completionHandler:^(NSArray<WPTrackingDomain *> * domains, NSError *error) {
                if (error) {
                    RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request tracking domains from WebPrivacy");
                    return;
                }

                for (WPTrackingDomain *domain in domains)
                    list().set(String::fromLatin1([domain.host UTF8String]), TrackerDomainLookupInfo { domain });
            }];
        });
    }

    static const TrackerDomainLookupInfo find(String host)
    {
        if (!list().isValidKey(host))
            return { };
        return list().get(host);
    }

private:
    static MemoryCompactRobinHoodHashMap<String, TrackerDomainLookupInfo>& list()
    {
        static NeverDestroyed<MemoryCompactRobinHoodHashMap<String, TrackerDomainLookupInfo>> map;
        return map.get();
    }

    CString m_owner;
    CanBlock m_canBlock { CanBlock::No };
};

void configureForAdvancedPrivacyProtections(NSURLSession *session)
{
    static bool canSetTrackerLookupCallback = [&] {
        return [NSURLSession instancesRespondToSelector:@selector(_networkContext)];
    }();

    if (!canSetTrackerLookupCallback)
        return;

    auto context = session._networkContext;
    if (!context)
        return;

    TrackerAddressLookupInfo::populateIfNeeded();
    TrackerDomainLookupInfo::populateIfNeeded();

    auto* setTrackerLookupCallback = nw_context_set_tracker_lookup_callbackPtr();
    if (!setTrackerLookupCallback)
        return;

    setTrackerLookupCallback(context, ^(nw_endpoint_t endpoint, const char** hostName, const char** owner, bool* canBlock) {
        if (auto address = ipAddress(endpoint)) {
            if (auto* info = TrackerAddressLookupInfo::find(*address)) {
                *owner = info->owner().data();
                *hostName = info->host().data();
                *canBlock = info->canBlock() == TrackerAddressLookupInfo::CanBlock::Yes;
            }
        }

        if (auto host = hostname(endpoint)) {
            auto domain = WebCore::RegistrableDomain { URL { makeString("http://"_s, String::fromLatin1(*host)) } };
            if (auto info = TrackerDomainLookupInfo::find(domain.string()); info.owner().length()) {
                *owner = info.owner().data();
                *hostName = *host;
                *canBlock = info.canBlock() == TrackerDomainLookupInfo::CanBlock::Yes;
            }
        }
    });
}

bool isKnownTrackerAddressOrDomain(StringView host)
{
    TrackerAddressLookupInfo::populateIfNeeded();
    TrackerDomainLookupInfo::populateIfNeeded();

    if (auto address = URL::hostIsIPAddress(host) ? IPAddress::fromString(host.toStringWithoutCopying()) : std::nullopt) {
        if (TrackerAddressLookupInfo::find(*address))
            return true;
    }

    auto domain = WebCore::RegistrableDomain { URL { makeString("http://"_s, host) } };
    return TrackerDomainLookupInfo::find(domain.string()).owner().length();
}

#else

void configureForAdvancedPrivacyProtections(NSURLSession *) { }
bool isKnownTrackerAddressOrDomain(StringView) { return false; }

#endif

#if ENABLE(SCRIPT_TRACKING_PRIVACY_PROTECTIONS)

static WebCore::ScriptTrackingPrivacyFlags allowedScriptTrackingCategories(WPScriptAccessCategories categories)
{
    WebCore::ScriptTrackingPrivacyFlags result;
    if (categories & WPScriptAccessCategoryAudio)
        result.add(WebCore::ScriptTrackingPrivacyFlag::Audio);
    if (categories & WPScriptAccessCategoryCanvas)
        result.add(WebCore::ScriptTrackingPrivacyFlag::Canvas);
    if (categories & WPScriptAccessCategoryCookies)
        result.add(WebCore::ScriptTrackingPrivacyFlag::Cookies);
    if (categories & WPScriptAccessCategoryHardwareConcurrency)
        result.add(WebCore::ScriptTrackingPrivacyFlag::HardwareConcurrency);
    if (categories & WPScriptAccessCategoryLocalStorage)
        result.add(WebCore::ScriptTrackingPrivacyFlag::LocalStorage);
    if (categories & WPScriptAccessCategoryPayments)
        result.add(WebCore::ScriptTrackingPrivacyFlag::Payments);
    if (categories & WPScriptAccessCategoryQueryParameters)
        result.add(WebCore::ScriptTrackingPrivacyFlag::QueryParameters);
    if (categories & WPScriptAccessCategoryReferrer)
        result.add(WebCore::ScriptTrackingPrivacyFlag::Referrer);
    if (categories & WPScriptAccessCategoryScreenOrViewport)
        result.add(WebCore::ScriptTrackingPrivacyFlag::ScreenOrViewport);
    if (categories & WPScriptAccessCategorySpeech)
        result.add(WebCore::ScriptTrackingPrivacyFlag::Speech);
    if (categories & WPScriptAccessCategoryFormControls)
        result.add(WebCore::ScriptTrackingPrivacyFlag::FormControls);
    return result;
}

#endif // ENABLE(SCRIPT_TRACKING_PRIVACY_PROTECTIONS)

void ScriptTrackingPrivacyController::updateList(CompletionHandler<void()>&& completion)
{
    ASSERT(RunLoop::isMain());
#if ENABLE(SCRIPT_TRACKING_PRIVACY_PROTECTIONS)
    if (!PAL::isWebPrivacyFrameworkAvailable() || ![PAL::getWPResourcesClass() instancesRespondToSelector:@selector(requestFingerprintingScripts:completionHandler:)]) {
        RunLoop::mainSingleton().dispatch(WTFMove(completion));
        return;
    }

    static MainRunLoopNeverDestroyed<Vector<CompletionHandler<void()>, 1>> pendingCompletionHandlers;
    pendingCompletionHandlers->append(WTFMove(completion));
    if (pendingCompletionHandlers->size() > 1)
        return;

    RetainPtr options = adoptNS([PAL::allocWPResourceRequestOptionsInstance() init]);
    [options setAfterUpdates:NO];

    [[PAL::getWPResourcesClass() sharedInstance] requestFingerprintingScripts:options.get() completionHandler:^(NSArray<WPFingerprintingScript *> *scripts, NSError *error) {
        auto callCompletionHandlers = makeScopeExit([&] {
            for (auto& completionHandler : std::exchange(pendingCompletionHandlers.get(), { }))
                completionHandler();
        });

        if (error) {
            RELEASE_LOG_ERROR(ResourceLoadStatistics, "Failed to request known fingerprinting scripts from WebPrivacy: %@", error);
            return;
        }

        ScriptTrackingPrivacyRules result;
        for (WPFingerprintingScript *script in scripts) {
            static bool supportsAllowedCategories = [script respondsToSelector:@selector(allowedCategories)];
            auto allowedCategories = allowedScriptTrackingCategories(supportsAllowedCategories ? script.allowedCategories : WPScriptAccessCategoryNone);
            if (script.firstParty) {
                if (script.topDomain)
                    result.firstPartyTopDomains.append({ script.host, allowedCategories });
                else
                    result.firstPartyHosts.append({ script.host, allowedCategories });
            } else {
                if (script.topDomain)
                    result.thirdPartyTopDomains.append({ script.host, allowedCategories });
                else
                    result.thirdPartyHosts.append({ script.host, allowedCategories });
            }
        }
        setCachedListData(WTFMove(result));
    }];
#else
    RunLoop::mainSingleton().dispatch(WTFMove(completion));
#endif
}

WPResourceType ScriptTrackingPrivacyController::resourceType() const
{
    return WPResourceTypeFingerprintingScripts;
}

unsigned ScriptTrackingPrivacyController::resourceTypeValue() const
{
    return WPResourceTypeFingerprintingScripts;
}

void ScriptTrackingPrivacyController::didUpdateCachedListData()
{
    m_cachedListData.firstPartyTopDomains.shrinkToFit();
    m_cachedListData.firstPartyHosts.shrinkToFit();
    m_cachedListData.thirdPartyTopDomains.shrinkToFit();
    m_cachedListData.thirdPartyHosts.shrinkToFit();
}

} // namespace WebKit

#endif // ENABLE(ADVANCED_PRIVACY_PROTECTIONS)
