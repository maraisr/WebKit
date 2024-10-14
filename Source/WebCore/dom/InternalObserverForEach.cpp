/*
* Copyright (C) 2024 Marais Rossouw <me@marais.co>. All rights reserved.
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
#include "InternalObserverForEach.h"

#include "AbortController.h"
#include "AbortSignal.h"
#include "InternalObserver.h"
#include "JSDOMPromiseDeferred.h"
#include "Observable.h"
#include "ScriptExecutionContext.h"
#include "SubscribeOptions.h"
#include "Subscriber.h"
#include "SubscriberCallback.h"
#include "VisitorCallback.h"
#include <JavaScriptCore/JSCJSValueInlines.h>

namespace WebCore {

class InternalObserverForEach final : public InternalObserver {
public:
    static Ref<InternalObserverForEach> create(ScriptExecutionContext& context, Ref<VisitorCallback> callback, Ref<DeferredPromise>& promise)
    {
        Ref internalObserver = adoptRef(*new InternalObserverForEach(context, callback, promise));
        internalObserver->suspendIfNeeded();
        return internalObserver;
    }

    Ref<AbortSignal> signal() const { return m_signal; }

private:
    void next(JSC::JSValue value) final
    {
        auto* context = scriptExecutionContext();
        ASSERT(context);

        Ref vm = context->globalObject()->vm();

        {
            JSC::JSLockHolder lock(vm);

            // The exception is not reported, instead it is forwarded to the
            // abort signal and promise rejection.
            // As such, VisitorCallback `[RethrowsException]` and here a
            // catch scope is declared so the error can be passed to any promise
            // rejection handlers and the abort signal.
            auto scope = DECLARE_CATCH_SCOPE(vm);
            m_callback->handleEvent(value, m_idx++);
            JSC::Exception* exception = scope.exception();
            if (UNLIKELY(exception)) {
                scope.clearException();
                auto value = exception->value();
                m_promise->reject<IDLAny>(value);
                m_signal->signalAbort(value);
            }
        }
    }

    void error(JSC::JSValue value) final
    {
        m_promise->reject<IDLAny>(value);
    }

    void complete() final
    {
        InternalObserver::complete();
        m_promise->resolve();
    }

    void visitAdditionalChildren(JSC::AbstractSlotVisitor& visitor) const final
    {
        m_callback->visitJSFunction(visitor);
    }

    void visitAdditionalChildren(JSC::SlotVisitor& visitor) const final
    {
        m_callback->visitJSFunction(visitor);
    }

    InternalObserverForEach(ScriptExecutionContext& context, Ref<VisitorCallback> callback, Ref<DeferredPromise>& promise)
        : InternalObserver(context)
        , m_callback(callback)
        , m_signal(AbortSignal::create(&context))
        , m_promise(promise)
    {
    }

    uint64_t m_idx { 0 };
    Ref<VisitorCallback> m_callback;
    Ref<AbortSignal> m_signal;
    Ref<DeferredPromise> m_promise;
};

void createInternalObserverOperatorForEach(ScriptExecutionContext& context, Ref<Observable> observable, Ref<VisitorCallback> callback, SubscribeOptions options, Ref<DeferredPromise>& promise)
{
    auto observer = InternalObserverForEach::create(context, callback, promise);

    Vector<Ref<AbortSignal>> signals = { observer->signal() };
    if (UNLIKELY(options.signal)) { signals.append(*options.signal); }

    auto signal = AbortSignal::any(context, signals);

    if (UNLIKELY(signal->aborted())) { return promise->reject<IDLAny>(signal->reason().getValue()); }

    signal->addAlgorithm([promise](JSC::JSValue reason) {
        promise->reject<IDLAny>(reason);
    });

    observable->subscribeInternal(context, observer, SubscribeOptions { .signal = &signal.get() });
}

} // namespace WebCore
