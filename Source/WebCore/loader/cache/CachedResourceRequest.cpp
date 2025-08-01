/*
 * Copyright (C) 2012 Google, Inc. All rights reserved.
 * Copyright (C) 2015-2025 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. ``AS IS'' AND ANY
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
#include "CachedResourceRequest.h"

#include "CachePolicy.h"
#include "CachedResourceLoader.h"
#include "CachedResourceRequestInitiatorTypes.h"
#include "ContentExtensionsBackend.h"
#include "CrossOriginAccessControl.h"
#include "Document.h"
#include "Element.h"
#include "FrameLoader.h"
#include "HTTPHeaderValues.h"
#include "ImageDecoder.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "MIMETypeRegistry.h"
#include "MemoryCache.h"
#include "OriginAccessPatterns.h"
#include "Quirks.h"
#include "SecurityPolicy.h"
#include "ServiceWorkerRegistrationData.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/text/StringBuilder.h>

#if ENABLE(LOCKDOWN_MODE_API)
#import <pal/cocoa/LockdownModeCocoa.h>
#endif

namespace WebCore {

CachedResourceRequest::CachedResourceRequest(ResourceRequest&& resourceRequest, const ResourceLoaderOptions& options, std::optional<ResourceLoadPriority> priority, String&& charset)
    : m_resourceRequest(WTFMove(resourceRequest))
    , m_charset(WTFMove(charset))
    , m_options(options)
    , m_priority(priority)
    , m_fragmentIdentifier(splitFragmentIdentifierFromRequestURL(m_resourceRequest))
{
}

String CachedResourceRequest::splitFragmentIdentifierFromRequestURL(ResourceRequest& request)
{
    if (!MemoryCache::shouldRemoveFragmentIdentifier(request.url()))
        return { };
    URL url = request.url();
    auto fragmentIdentifier = url.fragmentIdentifier().toString();
    url.removeFragmentIdentifier();
    request.setURL(WTFMove(url));
    return fragmentIdentifier;
}

void CachedResourceRequest::setInitiator(Element& element)
{
    ASSERT(!m_initiatorElement);
    ASSERT(m_initiatorType.isEmpty());
    m_initiatorElement = element;
}

void CachedResourceRequest::setInitiatorType(const AtomString& type)
{
    ASSERT(!m_initiatorElement);
    ASSERT(m_initiatorType.isEmpty());
    m_initiatorType = type;
}

const AtomString& CachedResourceRequest::initiatorType() const
{
    if (m_initiatorElement)
        return m_initiatorElement->localName();
    if (!m_initiatorType.isEmpty())
        return m_initiatorType;

    static MainThreadNeverDestroyed<const AtomString> defaultName("other"_s);
    return defaultName;
}

void CachedResourceRequest::updateForAccessControl(Document& document)
{
    ASSERT(m_options.mode == FetchOptions::Mode::Cors);

    m_origin = document.securityOrigin();
    updateRequestForAccessControl(m_resourceRequest, *m_origin, m_options.storedCredentialsPolicy);
}

void upgradeInsecureResourceRequestIfNeeded(ResourceRequest& request, Document& document, ContentSecurityPolicy::AlwaysUpgradeRequest alwaysUpgradeRequest)
{
    URL url = request.url();

    ASSERT(document.contentSecurityPolicy());
    document.checkedContentSecurityPolicy()->upgradeInsecureRequestIfNeeded(url, ContentSecurityPolicy::InsecureRequestType::Load, alwaysUpgradeRequest);

    if (url == request.url())
        return;

    request.setURL(WTFMove(url));
}

void CachedResourceRequest::upgradeInsecureRequestIfNeeded(Document& document, ContentSecurityPolicy::AlwaysUpgradeRequest alwaysUpgradeRequest)
{
    upgradeInsecureResourceRequestIfNeeded(m_resourceRequest, document, alwaysUpgradeRequest);
}

void CachedResourceRequest::setDomainForCachePartition(Document& document)
{
    m_resourceRequest.setDomainForCachePartition(document.domainForCachePartition());
}

void CachedResourceRequest::setDomainForCachePartition(const String& domain)
{
    m_resourceRequest.setDomainForCachePartition(domain);
}

static inline void appendAdditionalSupportedImageMIMETypes(StringBuilder& acceptHeader)
{
    for (const auto& additionalSupportedImageMIMEType : MIMETypeRegistry::additionalSupportedImageMIMETypes()) {
        acceptHeader.append(additionalSupportedImageMIMEType);
        acceptHeader.append(',');
    }
}

static inline void appendVideoImageResource(StringBuilder& acceptHeader)
{
    if (ImageDecoder::supportsMediaType(ImageDecoder::MediaType::Video))
        acceptHeader.append("video/*;q=0.8,"_s);
}

static String acceptHeaderValueForImageResource(bool usingSecureProtocol)
{
    static MainThreadNeverDestroyed<String> staticPrefix = [] {
        StringBuilder builder;
        builder.append("image/webp,"_s);
#if HAVE(AVIF) || USE(AVIF)
        builder.append("image/avif,"_s);
#endif
#if HAVE(JPEGXL) || USE(JPEGXL)
        builder.append("image/jxl,"_s);
#endif
#if HAVE(HEIC)
        builder.append("image/heic,image/heic-sequence,"_s);
#endif
        return builder.toString();
    }();

#if ENABLE(LOCKDOWN_MODE_API)
    bool limitToLockdownModeSet = usingSecureProtocol && PAL::isLockdownModeEnabledForCurrentProcess();
#else
    static bool limitToLockdownModeSet = false;
    UNUSED_PARAM(usingSecureProtocol);
#endif

    StringBuilder builder;
    if (limitToLockdownModeSet)
        builder.append("image/webp,"_s);
    else {
        builder.append(staticPrefix.get());
        appendAdditionalSupportedImageMIMETypes(builder);
    }
    appendVideoImageResource(builder);
    builder.append("image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5"_s);
    return builder.toString();
}

String CachedResourceRequest::acceptHeaderValueFromType(CachedResource::Type type, bool usingSecureProtocol)
{
    switch (type) {
    case CachedResource::Type::MainResource:
        return "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"_s;
    case CachedResource::Type::ImageResource:
        return acceptHeaderValueForImageResource(usingSecureProtocol);
    case CachedResource::Type::CSSStyleSheet:
        return "text/css,*/*;q=0.1"_s;
    case CachedResource::Type::SVGDocumentResource:
        return "image/svg+xml"_s;
#if ENABLE(XSLT)
    case CachedResource::Type::XSLStyleSheet:
        // FIXME: This should accept more general xml formats */*+xml, image/svg+xml for example.
        return "text/xml,application/xml,application/xhtml+xml,text/xsl,application/rss+xml,application/atom+xml"_s;
#endif
    default:
        return "*/*"_s;
    }
}

void CachedResourceRequest::setAcceptHeaderIfNone(CachedResource::Type type)
{
    if (!m_resourceRequest.hasHTTPHeader(HTTPHeaderName::Accept))
        m_resourceRequest.setHTTPHeaderField(HTTPHeaderName::Accept, acceptHeaderValueFromType(type, m_resourceRequest.url().protocolIsSecure()));
}

void CachedResourceRequest::disableCachingIfNeeded()
{
    if (m_options.cache == FetchOptions::Cache::NoStore)
        m_options.cachingPolicy = CachingPolicy::DisallowCaching;
}

void CachedResourceRequest::updateAccordingCacheMode()
{
    if (m_options.cache == FetchOptions::Cache::Default
        && (m_resourceRequest.hasHTTPHeaderField(HTTPHeaderName::IfModifiedSince)
            || m_resourceRequest.hasHTTPHeaderField(HTTPHeaderName::IfNoneMatch)
            || m_resourceRequest.hasHTTPHeaderField(HTTPHeaderName::IfUnmodifiedSince)
            || m_resourceRequest.hasHTTPHeaderField(HTTPHeaderName::IfMatch)
            || m_resourceRequest.hasHTTPHeaderField(HTTPHeaderName::IfRange)))
        m_options.cache = FetchOptions::Cache::NoStore;

    switch (m_options.cache) {
    case FetchOptions::Cache::NoCache:
        m_resourceRequest.setCachePolicy(ResourceRequestCachePolicy::RefreshAnyCacheData);
        m_resourceRequest.addHTTPHeaderFieldIfNotPresent(HTTPHeaderName::CacheControl, HTTPHeaderValues::maxAge0());
        break;
    case FetchOptions::Cache::NoStore:
        m_resourceRequest.setCachePolicy(ResourceRequestCachePolicy::DoNotUseAnyCache);
        m_resourceRequest.addHTTPHeaderFieldIfNotPresent(HTTPHeaderName::Pragma, HTTPHeaderValues::noCache());
        m_resourceRequest.addHTTPHeaderFieldIfNotPresent(HTTPHeaderName::CacheControl, HTTPHeaderValues::noCache());
        break;
    case FetchOptions::Cache::Reload:
        m_resourceRequest.setCachePolicy(ResourceRequestCachePolicy::ReloadIgnoringCacheData);
        m_resourceRequest.addHTTPHeaderFieldIfNotPresent(HTTPHeaderName::Pragma, HTTPHeaderValues::noCache());
        m_resourceRequest.addHTTPHeaderFieldIfNotPresent(HTTPHeaderName::CacheControl, HTTPHeaderValues::noCache());
        break;
    case FetchOptions::Cache::Default:
        break;
    case FetchOptions::Cache::ForceCache:
        m_resourceRequest.setCachePolicy(ResourceRequestCachePolicy::ReturnCacheDataElseLoad);
        break;
    case FetchOptions::Cache::OnlyIfCached:
        m_resourceRequest.setCachePolicy(ResourceRequestCachePolicy::ReturnCacheDataDontLoad);
        break;
    }
}

void CachedResourceRequest::updateCacheModeIfNeeded(CachePolicy cachePolicy)
{
    if (cachePolicy == CachePolicy::Reload && m_options.cache == FetchOptions::Cache::Default && m_options.cachingPolicy == CachingPolicy::AllowCaching)
        m_options.cache = FetchOptions::Cache::Reload;
}

void CachedResourceRequest::updateAcceptEncodingHeader()
{
    if (!m_resourceRequest.hasHTTPHeaderField(HTTPHeaderName::Range))
        return;

    // FIXME: rdar://problem/40879225. Media engines triggering the load should not set this Accept-Encoding header.
    ASSERT(!m_resourceRequest.hasHTTPHeaderField(HTTPHeaderName::AcceptEncoding) || m_options.destination == FetchOptions::Destination::Audio || m_options.destination == FetchOptions::Destination::Video);

    m_resourceRequest.addHTTPHeaderFieldIfNotPresent(HTTPHeaderName::AcceptEncoding, "identity"_s);
}

void CachedResourceRequest::removeFragmentIdentifierIfNeeded()
{
    URL url = MemoryCache::removeFragmentIdentifierIfNeeded(m_resourceRequest.url());
    if (url.string() != m_resourceRequest.url())
        m_resourceRequest.setURL(WTFMove(url));
}

#if ENABLE(CONTENT_EXTENSIONS)

void CachedResourceRequest::applyResults(ContentRuleListResults&& results, Page* page)
{
    ContentExtensions::applyResultsToRequest(WTFMove(results), page, m_resourceRequest);
}

#endif

void CachedResourceRequest::updateReferrerPolicy(ReferrerPolicy defaultPolicy)
{
    if (m_options.referrerPolicy == ReferrerPolicy::EmptyString)
        m_options.referrerPolicy = defaultPolicy;
}

void CachedResourceRequest::updateReferrerAndOriginHeaders(FrameLoader& frameLoader)
{
    // Implementing step 9 to 11 of https://fetch.spec.whatwg.org/#http-network-or-cache-fetch as of 16 March 2018
    URL outgoingReferrerURL;
    if (m_resourceRequest.hasHTTPReferrer())
        outgoingReferrerURL = URL { m_resourceRequest.httpReferrer() };
    else
        outgoingReferrerURL = frameLoader.outgoingReferrerURL();
    updateRequestReferrer(m_resourceRequest, m_options.referrerPolicy, outgoingReferrerURL, OriginAccessPatternsForWebProcess::singleton());

    if (!m_resourceRequest.httpOrigin().isEmpty())
        return;

    auto* document = frameLoader.frame().document();
    auto actualOrigin = (document && m_options.destination == FetchOptionsDestination::EmptyString && m_initiatorType == cachedResourceRequestInitiatorTypes().fetch) ? Ref { document->securityOrigin() } : SecurityOrigin::create(outgoingReferrerURL);
    String outgoingOrigin;
    if (m_options.mode == FetchOptions::Mode::Cors)
        outgoingOrigin = actualOrigin->toString();
    else
        outgoingOrigin = SecurityPolicy::generateOriginHeader(m_options.referrerPolicy, m_resourceRequest.url(), actualOrigin, OriginAccessPatternsForWebProcess::singleton());

    FrameLoader::addHTTPOriginIfNeeded(m_resourceRequest, outgoingOrigin);
}

void CachedResourceRequest::updateUserAgentHeader(FrameLoader& frameLoader)
{
    frameLoader.applyUserAgentIfNeeded(m_resourceRequest);
}

bool isRequestCrossOrigin(SecurityOrigin* origin, const URL& requestURL, const ResourceLoaderOptions& options)
{
    if (!origin)
        return false;

    // Using same origin mode guarantees the loader will not do a cross-origin load, so we let it take care of it and just return false.
    if (options.mode == FetchOptions::Mode::SameOrigin)
        return false;

    // FIXME: We should remove options.sameOriginDataURLFlag once https://github.com/whatwg/fetch/issues/393 is fixed.
    if (requestURL.protocolIsData() && options.sameOriginDataURLFlag == SameOriginDataURLFlag::Set)
        return false;

    return !origin->canRequest(requestURL, OriginAccessPatternsForWebProcess::singleton());
}

void CachedResourceRequest::setDestinationIfNotSet(FetchOptions::Destination destination)
{
    if (m_options.destination != FetchOptions::Destination::EmptyString)
        return;
    m_options.destination = destination;
}

void CachedResourceRequest::setClientIdentifierIfNeeded(ScriptExecutionContextIdentifier clientIdentifier)
{
    if (!m_options.clientIdentifier)
        m_options.clientIdentifier = clientIdentifier.object();
}

void CachedResourceRequest::setSelectedServiceWorkerRegistrationIdentifierIfNeeded(ServiceWorkerRegistrationIdentifier identifier)
{
    if (isNonSubresourceRequest(m_options.destination))
        return;
    if (isPotentialNavigationOrSubresourceRequest(m_options.destination))
        return;

    if (m_options.serviceWorkersMode == ServiceWorkersMode::None)
        return;
    if (m_options.serviceWorkerRegistrationIdentifier)
        return;

    m_options.serviceWorkerRegistrationIdentifier = identifier;
}

void CachedResourceRequest::setNavigationServiceWorkerRegistrationData(const std::optional<ServiceWorkerRegistrationData>& data)
{
    if (!data || !data->activeWorker) {
        m_options.serviceWorkersMode = ServiceWorkersMode::None;
        return;
    }
    m_options.serviceWorkerRegistrationIdentifier = data->identifier;
}

} // namespace WebCore
