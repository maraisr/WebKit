/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
#include <wtf/RunLoop.h>

#include <wtf/NeverDestroyed.h>
#include <wtf/Ref.h>
#include <wtf/StdLibExtras.h>
#include <wtf/threads/BinarySemaphore.h>

namespace WTF {

SUPPRESS_UNCOUNTED_LOCAL static RunLoop* s_mainRunLoop;
#if USE(WEB_THREAD)
SUPPRESS_UNCOUNTED_LOCAL static RunLoop* s_webRunLoop;
#endif

// Helper class for ThreadSpecificData.
class RunLoop::Holder {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(RunLoop);
public:
    Holder()
        : m_runLoop(adoptRef(*new RunLoop))
    {
    }

    ~Holder()
    {
        m_runLoop->threadWillExit();
    }

    RunLoop& runLoop() { return m_runLoop; }

private:
    const Ref<RunLoop> m_runLoop;
};

void RunLoop::initializeMain()
{
    RELEASE_ASSERT(!s_mainRunLoop);
    s_mainRunLoop = &RunLoop::currentSingleton();
}

auto RunLoop::runLoopHolder() -> ThreadSpecific<Holder>&
{
    static LazyNeverDestroyed<ThreadSpecific<Holder>> runLoopHolder;
    static std::once_flag onceKey;
    std::call_once(onceKey, [&] {
        runLoopHolder.construct();
    });
    return runLoopHolder;
}

RunLoop& RunLoop::currentSingleton()
{
    return runLoopHolder()->runLoop();
}

RunLoop& RunLoop::mainSingleton()
{
    ASSERT(s_mainRunLoop);
    return *s_mainRunLoop;
}

#if USE(WEB_THREAD)
void RunLoop::initializeWeb()
{
    RELEASE_ASSERT(!s_webRunLoop);
    s_webRunLoop = &RunLoop::currentSingleton();
}

RunLoop& RunLoop::webSingleton()
{
    ASSERT(s_webRunLoop);
    return *s_webRunLoop;
}

RunLoop* RunLoop::webIfExists()
{
    return s_webRunLoop;
}
#endif

Ref<RunLoop> RunLoop::create(ASCIILiteral threadName, ThreadType threadType, Thread::QOS qos)
{
    RefPtr<RunLoop> runLoop;
    BinarySemaphore semaphore;
    Thread::create(threadName, [&] SUPPRESS_UNCOUNTED_LAMBDA_CAPTURE {
        runLoop = &RunLoop::currentSingleton();
        semaphore.signal();
        runLoop->run();
    }, threadType, qos)->detach();
    semaphore.wait();
    return runLoop.releaseNonNull();
}

bool RunLoop::isCurrent() const
{
    // Avoid constructing the RunLoop for the current thread if it has not been created yet.
    return runLoopHolder().isSet() && this == &RunLoop::currentSingleton();
}

void RunLoop::performWork()
{
    bool didSuspendFunctions = false;

    {
        Locker locker { m_nextIterationLock };

        // If the RunLoop re-enters or re-schedules, we're expected to execute all functions in order.
        while (!m_currentIteration.isEmpty())
            m_nextIteration.prepend(m_currentIteration.takeLast());

        m_currentIteration = std::exchange(m_nextIteration, { });
    }

    while (!m_currentIteration.isEmpty()) {
        if (m_isFunctionDispatchSuspended) {
            didSuspendFunctions = true;
            break;
        }

        auto function = m_currentIteration.takeFirst();
        function();
    }

    // Suspend only for a single cycle.
    m_isFunctionDispatchSuspended = false;
    m_hasSuspendedFunctions = didSuspendFunctions;

    if (m_hasSuspendedFunctions)
        wakeUp();
}

void RunLoop::dispatch(Function<void()>&& function)
{
    RELEASE_ASSERT(function);
    bool needsWakeup = false;

    {
        Locker locker { m_nextIterationLock };
        needsWakeup = m_nextIteration.isEmpty();
        m_nextIteration.append(WTFMove(function));
    }

    if (needsWakeup)
        wakeUp();
}

Ref<RunLoop::DispatchTimer> RunLoop::dispatchAfter(Seconds delay, Function<void()>&& function)
{
    RELEASE_ASSERT(function);
    Ref<DispatchTimer> timer = adoptRef(*new DispatchTimer(*this));
    timer->setFunction([timer = timer.copyRef(), function = WTFMove(function)]() mutable {
        Ref<DispatchTimer> protectedTimer { WTFMove(timer) };
        function();
        protectedTimer->stop();
    });
    timer->startOneShot(delay);
    return timer;
}

void RunLoop::suspendFunctionDispatchForCurrentCycle()
{
    // Don't suspend if there are already suspended functions to avoid unexecuted function pile-up.
    if (m_isFunctionDispatchSuspended || m_hasSuspendedFunctions)
        return;

    m_isFunctionDispatchSuspended = true;
    // Wake up (even if there is nothing to do) to disable suspension.
    wakeUp();
}

void RunLoop::threadWillExit()
{
    m_currentIteration.clear();
    {
        Locker locker { m_nextIterationLock };
        m_nextIteration.clear();
    }
}

} // namespace WTF
