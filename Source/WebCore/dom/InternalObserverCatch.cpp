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
#include "InternalObserverCatch.h"

#include "InternalObserver.h"
#include "JSSubscriptionObserverCallback.h"
#include "Observable.h"
#include "SubscribeOptions.h"
#include "Subscriber.h"
#include "SubscriberCallback.h"
#include <JavaScriptCore/JSCJSValueInlines.h>

namespace WebCore {

class InternalObserverCatch final : public InternalObserver {
public:
    static Ref<InternalObserverCatch> create(ScriptExecutionContext& context, Ref<Subscriber>&& subscriber, Ref<CatchCallback> callback)
    {
        Ref internalObserver = adoptRef(*new InternalObserverCatch(context, WTFMove(subscriber), WTFMove(callback)));
        internalObserver->suspendIfNeeded();
        return internalObserver;
    }

    class SubscriberCallbackCatch final : public SubscriberCallback {
    public:
        static Ref<SubscriberCallbackCatch> create(ScriptExecutionContext& context, Ref<Observable>&& source, Ref<CatchCallback> callback)
        {
            return adoptRef(*new SubscriberCallbackCatch(context, WTFMove(source), WTFMove(callback)));
        }

        CallbackResult<void> handleEvent(Subscriber& subscriber) final
        {
            RefPtr context = scriptExecutionContext();

            if (!context) {
                subscriber.complete();
                return { };
            }

            Ref { m_sourceObservable }->subscribeInternal(*context, InternalObserverCatch::create(*context, subscriber, m_catch), SubscribeOptions { });

            return { };
        }

        CallbackResult<void> handleEventRethrowingException(Subscriber& subscriber) final
        {
            return handleEvent(subscriber);
        }

    private:
        bool hasCallback() const final { return true; }

        SubscriberCallbackCatch(ScriptExecutionContext& context, Ref<Observable>&& source, Ref<CatchCallback>&& callback)
            : SubscriberCallback(&context)
            , m_sourceObservable(WTFMove(source))
            , m_catch(WTFMove(callback))
        { }

        Ref<Observable> m_sourceObservable;
        Ref<CatchCallback> m_catch;
    };

private:
    void next(JSC::JSValue value) final
    {
        protectedSubscriber()->next(value);
    }

    void error(JSC::JSValue value) final
    {
        auto* globalObject = protectedScriptExecutionContext()->globalObject();
        ASSERT(globalObject);

        Ref vm = globalObject->vm();

        JSC::JSLockHolder lock(vm);

        auto scope = DECLARE_CATCH_SCOPE(vm);

        auto result = Ref { m_catch }->handleEventRethrowingException(value);

        JSC::Exception* exception = scope.exception();
        if (UNLIKELY(exception)) {
            scope.clearException();
            auto value = exception->value();
            return;
        }

        if (result.type() == CallbackResultType::Success) {

        }
    }

    void complete() final
    {
        InternalObserver::complete();
        protectedSubscriber()->complete();
    }

    void visitAdditionalChildren(JSC::AbstractSlotVisitor& visitor) const final
    {
        protectedSubscriber()->visitAdditionalChildren(visitor);
    }

    void visitAdditionalChildren(JSC::SlotVisitor& visitor) const final
    {
        protectedSubscriber()->visitAdditionalChildren(visitor);
    }

    Ref<Subscriber> protectedSubscriber() const
    {
        return m_subscriber;
    }

    InternalObserverCatch(ScriptExecutionContext& context, Ref<Subscriber>&& subscriber, Ref<CatchCallback>&& callback)
        : InternalObserver(context)
        , m_subscriber(WTFMove(subscriber))
        , m_catch(WTFMove(callback))
    {
    }

    Ref<Subscriber> m_subscriber;
    Ref<CatchCallback> m_catch;
};

Ref<SubscriberCallback> createSubscriberCallbackCatch(ScriptExecutionContext& context, Ref<Observable>&& observable, Ref<CatchCallback>&& callback)
{
    return InternalObserverCatch::SubscriberCallbackCatch::create(context, WTFMove(observable), WTFMove(callback));
}

} // namespace WebCore
