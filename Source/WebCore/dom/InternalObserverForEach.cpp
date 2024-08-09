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

private:
    void next(JSC::JSValue value) final
    {
        auto context = scriptExecutionContext();
        ASSERT(context);

        Ref vm = context->globalObject()->vm();
        JSC::JSLockHolder lock(vm);

        {
            auto scope = DECLARE_CATCH_SCOPE(vm);

            m_callback->handleEvent(value, m_idx++);

            if (UNLIKELY(scope.exception())) {
                auto* exception = scope.exception();
                scope.clearException();
                m_promise->reject<IDLAny>(exception->value());
                return;
            }
        }
    }

    void error(JSC::JSValue value) final
    {
        m_promise->reject<IDLAny>(value);
    }

    void complete() final {
        InternalObserver::complete();
        m_promise->resolve<IDLAny>(JSC::jsUndefined());
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
        , m_promise(promise)
    {
    }

    uint64_t m_idx { 0 };
    Ref<VisitorCallback> m_callback;
    Ref<DeferredPromise> m_promise;
};

void createInternalObserverOperatorForEach(ScriptExecutionContext& context, Ref<Observable> observable, Ref<VisitorCallback> callback, SubscribeOptions options, Ref<DeferredPromise>& promise)
{
    // TODO: create the dependant abort signal
    (void)options;
    Ref callbackSignal = AbortSignal::create(&context);
    SubscribeOptions internalOptions;
    internalOptions.signal = WTFMove(callbackSignal);

    auto observer = InternalObserverForEach::create(context, callback, promise);
    observable->subscribeInternal(context, observer, internalOptions);
}

} // namespace WebCore
