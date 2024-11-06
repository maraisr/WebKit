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
#include "InternalObserverInspect.h"

#include "InternalObserver.h"
#include "JSSubscriptionObserverCallback.h"
#include "Observable.h"
#include "ObservableInspector.h"
#include "ObservableInspectorAbortCallback.h"
#include "ScriptExecutionContext.h"
#include "SubscribeOptions.h"
#include "Subscriber.h"
#include "SubscriberCallback.h"
#include <JavaScriptCore/JSCJSValueInlines.h>

namespace WebCore {

class InternalObserverInspect final : public InternalObserver {
public:
    static Ref<InternalObserverInspect> create(ScriptExecutionContext& context, Ref<Subscriber> subscriber, const ObservableInspector& inspector)
    {
        Ref internalObserver = adoptRef(*new InternalObserverInspect(context, subscriber, inspector));
        internalObserver->suspendIfNeeded();
        return internalObserver;
    }

    class SubscriberCallbackInspect final : public SubscriberCallback {
    public:
        static Ref<SubscriberCallbackInspect> create(ScriptExecutionContext& context, Ref<Observable> source, const ObservableInspector& inspector)
        {
            return adoptRef(*new InternalObserverInspect::SubscriberCallbackInspect(context, source, inspector));
        }

        CallbackResult<void> handleEvent(Subscriber& subscriber) final
        {
            RefPtr context = scriptExecutionContext();

            if (!context) {
                subscriber.complete();
                return { };
            }

            if (m_inspector.subscribe) {
                auto* globalObject = protectedScriptExecutionContext()->globalObject();
                ASSERT(globalObject);

                Ref vm = globalObject->vm();
                JSC::JSLockHolder lock(vm);
                auto scope = DECLARE_CATCH_SCOPE(vm);

                m_inspector.subscribe->handleEvent();

                JSC::Exception* exception = scope.exception();
                if (UNLIKELY(exception)) {
                    scope.clearException();
                    subscriber.error(exception->value());
                    return { };
                }
            }

            SubscribeOptions options;
            options.signal = &subscriber.signal();
            m_sourceObservable->subscribeInternal(*context, InternalObserverInspect::create(*context, subscriber, m_inspector), options);

            return { };
        }

    private:
        bool hasCallback() const final { return true; }

        SubscriberCallbackInspect(ScriptExecutionContext& context, Ref<Observable> source, const ObservableInspector& inspector)
            : SubscriberCallback(&context)
            , m_sourceObservable(source)
            , m_inspector(inspector)
        { }

        Ref<Observable> m_sourceObservable;
        ObservableInspector m_inspector;
    };

private:
    void next(JSC::JSValue value) final
    {
        if (m_inspector.next) {
            Ref vm = protectedGlobalObjectVM();
            JSC::JSLockHolder lock(vm);
            auto scope = DECLARE_CATCH_SCOPE(vm);

            m_inspector.next->handleEvent(value);

            JSC::Exception* exception = scope.exception();
            if (UNLIKELY(exception)) {
                scope.clearException();
                m_subscriber->error(exception->value());
                return;
            }
        }

        m_subscriber->next(value);
    }

    void error(JSC::JSValue value) final
    {
        // removeAbortHandler();

        if (m_inspector.error) {
            Ref vm = protectedGlobalObjectVM();
            JSC::JSLockHolder lock(vm);
            auto scope = DECLARE_CATCH_SCOPE(vm);

            m_inspector.error->handleEvent(value);

            JSC::Exception* exception = scope.exception();
            if (UNLIKELY(exception)) {
                scope.clearException();
                m_subscriber->error(exception->value());
                return;
            }
        }

        m_subscriber->error(value);
    }

    void complete() final
    {
        InternalObserver::complete();

        // removeAbortHandler();

        if (m_inspector.complete) {
            Ref vm = protectedGlobalObjectVM();
            JSC::JSLockHolder lock(vm);
            auto scope = DECLARE_CATCH_SCOPE(vm);

            m_inspector.complete->handleEvent();

            JSC::Exception* exception = scope.exception();
            if (UNLIKELY(exception)) {
                scope.clearException();
                m_subscriber->error(exception->value());
                return;
            }
        }

        m_subscriber->complete();
    }

    void visitAdditionalChildren(JSC::AbstractSlotVisitor& visitor) const final
    {
        m_subscriber->visitAdditionalChildren(visitor);
        if (m_inspector.next)
            m_inspector.next->visitJSFunction(visitor);
        if (m_inspector.error)
            m_inspector.error->visitJSFunction(visitor);
        if (m_inspector.complete)
            m_inspector.complete->visitJSFunction(visitor);
        if (m_inspector.subscribe)
            m_inspector.subscribe->visitJSFunction(visitor);
        if (m_inspector.abort)
            m_inspector.abort->visitJSFunction(visitor);
    }

    void visitAdditionalChildren(JSC::SlotVisitor& visitor) const final
    {
        m_subscriber->visitAdditionalChildren(visitor);
        if (m_inspector.next)
            m_inspector.next->visitJSFunction(visitor);
        if (m_inspector.error)
            m_inspector.error->visitJSFunction(visitor);
        if (m_inspector.complete)
            m_inspector.complete->visitJSFunction(visitor);
        if (m_inspector.subscribe)
            m_inspector.subscribe->visitJSFunction(visitor);
        if (m_inspector.abort)
            m_inspector.abort->visitJSFunction(visitor);
    }

    // void removeAbortHandler()
    // {
    //     m_subscriber->protectedSignal()->removeAlgorithm(m_abortAlgorithmHandler);
    // }

    Ref<JSC::VM> protectedGlobalObjectVM() const
    {
        auto* globalObject = protectedScriptExecutionContext()->globalObject();
        ASSERT(globalObject);
        return globalObject->vm();
    }

    InternalObserverInspect(ScriptExecutionContext& context, Ref<Subscriber> subscriber, const ObservableInspector& inspector)
        : InternalObserver(context)
        , m_subscriber(subscriber)
        , m_inspector(inspector)
    {
        // m_abortAlgorithmHandler = m_subscriber->protectedSignal()->addAlgorithm([this, protectedThis = Ref { *this }](JSC::JSValue reason) {
        //     // TODO: Do this outside this callback
        //     if (!m_inspector.abort) return;

        //     auto exception = wrapWithExceptionSteps([&] {
        //         m_inspector.abort->handleEvent(reason);
        //     });

        //     if (exception) {
        //         auto* globalObject = protectedScriptExecutionContext()->globalObject();
        //         ASSERT(globalObject);

        //         reportException(globalObject, exception);
        //     }
        // });
    }

    Ref<Subscriber> m_subscriber;
    ObservableInspector m_inspector;
    // uint32_t m_abortAlgorithmHandler;
};

Ref<SubscriberCallback> createSubscriberCallbackInspect(ScriptExecutionContext& context, Ref<Observable> observable, RefPtr<JSSubscriptionObserverCallback> next)
{
    return InternalObserverInspect::SubscriberCallbackInspect::create(context, observable, ObservableInspector { .next = next });
}

Ref<SubscriberCallback> createSubscriberCallbackInspect(ScriptExecutionContext& context, Ref<Observable> observable, const ObservableInspector& inspector)
{
    return InternalObserverInspect::SubscriberCallbackInspect::create(context, observable, inspector);
}

} // namespace WebCore
