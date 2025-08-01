/*
 * Copyright (C) 2016 Canon Inc.
 * Copyright (C) 2020-2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted, provided that the following conditions
 * are required to be met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Canon Inc. nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CANON INC. AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CANON INC. AND ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FetchBodyOwner.h"

#include "ContextDestructionObserverInlines.h"
#include "Document.h"
#include "FetchLoader.h"
#include "HTTPParsers.h"
#include "HTTPStatusCodes.h"
#include "JSBlob.h"
#include "JSDOMFormData.h"
#include "JSDOMPromiseDeferred.h"
#include "ResourceError.h"
#include "ResourceResponse.h"
#include "WindowEventLoop.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

DEFINE_ALLOCATOR_WITH_HEAP_IDENTIFIER(FetchBodyOwner);
WTF_MAKE_TZONE_ALLOCATED_IMPL(FetchBodyOwner::BlobLoader);

FetchBodyOwner::FetchBodyOwner(ScriptExecutionContext* context, std::optional<FetchBody>&& body, Ref<FetchHeaders>&& headers)
    : ActiveDOMObject(context)
    , m_body(WTFMove(body))
    , m_headers(WTFMove(headers))
{
}

FetchBodyOwner::~FetchBodyOwner()
{
    if (RefPtr readableStreamSource = m_readableStreamSource)
        readableStreamSource->detach();
}

void FetchBodyOwner::stop()
{
    m_readableStreamSource = nullptr;
    if (m_body)
        m_body->cleanConsumer();

    if (m_blobLoader) {
        bool isUniqueReference = hasOneRef();
        if (CheckedPtr loader = m_blobLoader->loader.get())
            loader->stop();
        // After that point, 'this' may be destroyed, since unsetPendingActivity should have been called.
        ASSERT_UNUSED(isUniqueReference, isUniqueReference || !m_blobLoader);
    }
}

bool FetchBodyOwner::isDisturbed() const
{
    if (isBodyNull())
        return false;

    if (m_isDisturbed)
        return true;

    if (RefPtr readableStream = body().readableStream())
        return readableStream->isDisturbed();

    return false;
}

bool FetchBodyOwner::isDisturbedOrLocked() const
{
    if (isBodyNull())
        return false;

    if (m_isDisturbed)
        return true;

    if (RefPtr readableStream = body().readableStream())
        return readableStream->isDisturbed() || readableStream->isLocked();

    return false;
}

void FetchBodyOwner::arrayBuffer(Ref<DeferredPromise>&& promise)
{
    if (auto exception = loadingException()) {
        promise->reject(*exception);
        return;
    }

    if (isBodyNullOrOpaque()) {
        fulfillPromiseWithArrayBufferFromSpan(WTFMove(promise), { });
        return;
    }
    if (isDisturbedOrLocked()) {
        promise->reject(Exception { ExceptionCode::TypeError, "Body is disturbed or locked"_s });
        return;
    }
    m_isDisturbed = true;
    m_body->arrayBuffer(*this, WTFMove(promise));
}

void FetchBodyOwner::blob(Ref<DeferredPromise>&& promise)
{
    if (auto exception = loadingException()) {
        promise->reject(*exception);
        return;
    }

    if (isBodyNullOrOpaque()) {
        promise->resolveCallbackValueWithNewlyCreated<IDLInterface<Blob>>([this](auto& context) {
            return Blob::create(&context, Vector<uint8_t> { }, Blob::normalizedContentType(extractMIMETypeFromMediaType(contentType())));
        });
        return;
    }
    if (isDisturbedOrLocked()) {
        promise->reject(Exception { ExceptionCode::TypeError, "Body is disturbed or locked"_s });
        return;
    }
    m_isDisturbed = true;
    m_body->blob(*this, WTFMove(promise));
}

void FetchBodyOwner::bytes(Ref<DeferredPromise>&& promise)
{
    if (auto exception = loadingException()) {
        promise->reject(*exception);
        return;
    }

    if (isBodyNullOrOpaque()) {
        fulfillPromiseWithUint8ArrayFromSpan(WTFMove(promise), { });
        return;
    }
    if (isDisturbedOrLocked()) {
        promise->reject(Exception { ExceptionCode::TypeError, "Body is disturbed or locked"_s });
        return;
    }
    m_isDisturbed = true;
    m_body->bytes(*this, WTFMove(promise));
}

void FetchBodyOwner::cloneBody(FetchBodyOwner& owner)
{
    m_loadingError = owner.m_loadingError;
    if (owner.isBodyNull())
        return;
    m_body = owner.m_body->clone();
}

ExceptionOr<void> FetchBodyOwner::extractBody(FetchBody::Init&& value)
{
    auto currentContentType = contentType();
    bool isContentTypeSet = !currentContentType.isNull();
    auto result = FetchBody::extract(WTFMove(value), currentContentType);

    // Initialize the Content-Type header if it didn't exist.
    if (!isContentTypeSet && !currentContentType.isNull())
        m_headers->fastSet(HTTPHeaderName::ContentType, currentContentType);

    if (result.hasException())
        return result.releaseException();
    m_body = result.releaseReturnValue();
    return { };
}

void FetchBodyOwner::consumeOnceLoadingFinished(FetchBodyConsumer::Type type, Ref<DeferredPromise>&& promise)
{
    if (isDisturbedOrLocked()) {
        promise->reject(Exception { ExceptionCode::TypeError, "Body is disturbed or locked"_s });
        return;
    }
    m_isDisturbed = true;
    m_body->consumeOnceLoadingFinished(type, WTFMove(promise));
}

void FetchBodyOwner::formData(Ref<DeferredPromise>&& promise)
{
    if (auto exception = loadingException()) {
        promise->reject(*exception);
        return;
    }

    if (isDisturbedOrLocked()) {
        promise->reject(Exception { ExceptionCode::TypeError, "Body is disturbed or locked"_s });
        return;
    }

    if (isBodyNullOrOpaque()) {
        if (isBodyNull()) {
            // If the content-type is 'application/x-www-form-urlencoded', a body is not required and we should package an empty byte sequence as per the specification.
            if (auto formData = FetchBodyConsumer::packageFormData(promise->protectedScriptExecutionContext().get(), contentType(), { })) {
                promise->resolve<IDLInterface<DOMFormData>>(*formData);
                return;
            }
        }

        promise->reject(ExceptionCode::TypeError);
        return;
    }

    m_isDisturbed = true;
    m_body->formData(*this, WTFMove(promise));
}

void FetchBodyOwner::json(Ref<DeferredPromise>&& promise)
{
    if (auto exception = loadingException()) {
        promise->reject(*exception);
        return;
    }

    if (isBodyNullOrOpaque()) {
        promise->reject(ExceptionCode::SyntaxError);
        return;
    }
    if (isDisturbedOrLocked()) {
        promise->reject(Exception { ExceptionCode::TypeError, "Body is disturbed or locked"_s });
        return;
    }
    m_isDisturbed = true;
    m_body->json(*this, WTFMove(promise));
}

void FetchBodyOwner::text(Ref<DeferredPromise>&& promise)
{
    if (auto exception = loadingException()) {
        promise->reject(*exception);
        return;
    }

    if (isBodyNullOrOpaque()) {
        promise->resolve<IDLDOMString>({ });
        return;
    }
    if (isDisturbedOrLocked()) {
        promise->reject(Exception { ExceptionCode::TypeError, "Body is disturbed or locked"_s });
        return;
    }
    m_isDisturbed = true;
    m_body->text(*this, WTFMove(promise));
}

void FetchBodyOwner::loadBlob(const Blob& blob, FetchBodyConsumer* consumer)
{
    // Can only be called once for a body instance.
    ASSERT(!m_blobLoader);
    ASSERT(!isBodyNull());

    if (!scriptExecutionContext()) {
        m_body->loadingFailed(Exception { ExceptionCode::TypeError, "Blob loading failed"_s });
        return;
    }

    m_blobLoader.emplace(*this);
    m_blobLoader->loader = makeUnique<FetchLoader>(CheckedRef { *m_blobLoader }.get(), consumer);

    CheckedRef { *m_blobLoader->loader }->start(*protectedScriptExecutionContext(), blob);
    if (!m_blobLoader->loader->isStarted()) {
        m_body->loadingFailed(Exception { ExceptionCode::TypeError, "Blob loading failed"_s });
        m_blobLoader = std::nullopt;
        return;
    }
}

void FetchBodyOwner::finishBlobLoading()
{
    ASSERT(m_blobLoader);

    m_blobLoader = std::nullopt;
}

void FetchBodyOwner::blobLoadingSucceeded()
{
    ASSERT(!isBodyNull());
    if (RefPtr readableStreamSource = std::exchange(m_readableStreamSource, nullptr))
        readableStreamSource->close();

    m_body->loadingSucceeded(contentType());
    if (!m_blobLoader)
        return;

    finishBlobLoading();
}

void FetchBodyOwner::blobLoadingFailed()
{
    ASSERT(!isBodyNull());
    if (RefPtr readableStreamSource = std::exchange(m_readableStreamSource, nullptr)) {
        if (!readableStreamSource->isCancelling())
            readableStreamSource->error(Exception { ExceptionCode::TypeError, "Blob loading failed"_s });
    } else
        m_body->loadingFailed(Exception { ExceptionCode::TypeError, "Blob loading failed"_s });
    finishBlobLoading();
}

void FetchBodyOwner::blobChunk(const SharedBuffer& buffer)
{
    RefPtr readableStreamSource = m_readableStreamSource;
    ASSERT(readableStreamSource);
    if (!readableStreamSource->enqueue(buffer.tryCreateArrayBuffer()))
        stop();
}

FetchBodyOwner::BlobLoader::BlobLoader(FetchBodyOwner& owner)
    : owner(owner)
{
}

void FetchBodyOwner::BlobLoader::didReceiveResponse(const ResourceResponse& response)
{
    if (response.httpStatusCode() != httpStatus200OK)
        didFail({ });
}

void FetchBodyOwner::BlobLoader::didFail(const ResourceError&)
{
    // didFail might be called within FetchLoader::start call.
    if (loader->isStarted())
        protectedOwner()->blobLoadingFailed();
}

void FetchBodyOwner::BlobLoader::didSucceed(const NetworkLoadMetrics&)
{
    protectedOwner()->blobLoadingSucceeded();
}

ExceptionOr<RefPtr<ReadableStream>> FetchBodyOwner::readableStream(JSC::JSGlobalObject& state)
{
    if (isBodyNullOrOpaque())
        return nullptr;

    if (!m_body->hasReadableStream()) {
        auto voidOrException = createReadableStream(state);
        if (voidOrException.hasException()) [[unlikely]]
            return voidOrException.releaseException();
    }

    return m_body->readableStream();
}

ExceptionOr<void> FetchBodyOwner::createReadableStream(JSC::JSGlobalObject& state)
{
    ASSERT(!m_readableStreamSource);
    if (isDisturbed()) {
        auto streamOrException = ReadableStream::create(state, { }, { });
        if (streamOrException.hasException()) [[unlikely]]
            return streamOrException.releaseException();
        m_body->setReadableStream(streamOrException.releaseReturnValue());
        m_body->protectedReadableStream()->lock();
        return { };
    }

    m_readableStreamSource = adoptRef(*new FetchBodySource(*this));
    auto streamOrException = ReadableStream::create(*JSC::jsCast<JSDOMGlobalObject*>(&state), *m_readableStreamSource);
    if (streamOrException.hasException()) [[unlikely]] {
        m_readableStreamSource = nullptr;
        return streamOrException.releaseException();
    }
    m_body->setReadableStream(streamOrException.releaseReturnValue());
    return { };
}

void FetchBodyOwner::consumeBodyAsStream()
{
    RefPtr readableStreamSource = m_readableStreamSource;
    ASSERT(readableStreamSource);

    if (auto exception = loadingException()) {
        readableStreamSource->error(*exception);
        return;
    }

    body().consumeAsStream(*this, *readableStreamSource);
    if (!readableStreamSource->isPulling())
        m_readableStreamSource = nullptr;
}

ResourceError FetchBodyOwner::loadingError() const
{
    return WTF::switchOn(m_loadingError, [](const ResourceError& error) {
        return ResourceError { error };
    }, [](const Exception& exception) {
        return ResourceError { errorDomainWebKitInternal, 0, { }, exception.message() };
    }, [](auto&&) {
        return ResourceError { };
    });
}

std::optional<Exception> FetchBodyOwner::loadingException() const
{
    return WTF::switchOn(m_loadingError, [](const ResourceError& error) -> std::optional<Exception> {
        return Exception { ExceptionCode::TypeError, error.sanitizedDescription() };
    }, [](const Exception& exception) -> std::optional<Exception> {
        return Exception { exception };
    }, [](auto&&) -> std::optional<Exception> {
        return std::nullopt;
    });
}

bool FetchBodyOwner::virtualHasPendingActivity() const
{
    return !!m_blobLoader || (m_body && m_body->hasConsumerPendingActivity());
}

bool FetchBodyOwner::hasLoadingError() const
{
    return WTF::switchOn(m_loadingError, [](const ResourceError&) {
        return true;
    }, [](const Exception&) {
        return true;
    }, [](auto&&) {
        return false;
    });
}

void FetchBodyOwner::setLoadingError(Exception&& exception)
{
    if (hasLoadingError())
        return;

    m_loadingError = WTFMove(exception);
}

void FetchBodyOwner::setLoadingError(ResourceError&& error)
{
    if (hasLoadingError())
        return;

    m_loadingError = WTFMove(error);
}

} // namespace WebCore
