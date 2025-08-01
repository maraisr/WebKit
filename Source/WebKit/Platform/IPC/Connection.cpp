/*
 * Copyright (C) 2010-2016 Apple Inc. All rights reserved.
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
#include "Connection.h"

#include "Encoder.h"
#include "GeneratedSerializers.h"
#include "Logging.h"
#include "MessageFlags.h"
#include "MessageReceiveQueues.h"
#include "WorkQueueMessageReceiver.h"
#include <memory>
#include <wtf/ArgumentCoder.h>
#include <wtf/HashCountedSet.h>
#include <wtf/HashSet.h>
#include <wtf/Lock.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/ObjectIdentifier.h>
#include <wtf/RunLoop.h>
#include <wtf/Scope.h>
#include <wtf/SystemTracing.h>
#include <wtf/WTFProcess.h>
#include <wtf/text/WTFString.h>
#include <wtf/threads/BinarySemaphore.h>

#if PLATFORM(COCOA)
#include "MachMessage.h"
#endif

#if USE(UNIX_DOMAIN_SOCKETS)
#include "ArgumentCodersUnix.h"
#include "UnixMessage.h"
#endif

namespace IPC {

#if PLATFORM(COCOA)
// The IPC connection gets killed if the incoming message queue reaches 50000 messages before the main thread has a chance to dispatch them.
constexpr size_t maxPendingIncomingMessagesKillingThreshold { 50000 };
#endif

constexpr Seconds largeOutgoingMessageQueueTimeThreshold { 20_s };

std::atomic<unsigned> UnboundedSynchronousIPCScope::unboundedSynchronousIPCCount = 0;

enum class MessageIdentifierType { };
using MessageIdentifier = AtomicObjectIdentifier<MessageIdentifierType>;

#if ENABLE(UNFAIR_LOCK)
static UnfairLock s_connectionMapLock;
#else
static Lock s_connectionMapLock;
#endif

class Connection::SyncMessageState {
public:
    static std::unique_ptr<SyncMessageState, SyncMessageStateRelease> get(SerialFunctionDispatcher&);
    RefPtr<SerialFunctionDispatcher> dispatcher() { return m_dispatcher.get(); }

    void wakeUpClientRunLoop()
    {
        m_waitForSyncReplySemaphore.signal();
    }

    bool wait(Timeout timeout)
    {
        return m_waitForSyncReplySemaphore.waitUntil(timeout.deadline());
    }

    // Returns true if this message will be handled on a client thread that is currently
    // waiting for a reply to a synchronous message.
    bool processIncomingMessage(Connection& connectionForLockCheck, UniqueRef<Decoder>&) WTF_REQUIRES_LOCK(connectionForLockCheck.m_incomingMessagesLock);

    // Dispatch pending messages that should be dispatched while waiting for a sync reply.
    void dispatchMessages(Function<void(MessageName, uint64_t)>&& willDispatchMessage = { });

    // Dispatch pending messages that should be dispatched while waiting for a sync reply,
    // up until the message with the provided identifier.
    void dispatchMessagesUntil(MessageIdentifier lastMessageToDispatch);

    // Add matching pending messages to the provided MessageReceiveQueue.
    void enqueueMatchingMessages(Connection&, MessageReceiveQueue&, const ReceiverMatcher&);

    // Dispatch pending sync messages for given connection.
    void dispatchMessagesAndResetDidScheduleDispatchMessagesForConnection(Connection&);

    std::optional<MessageIdentifier> identifierOfLastMessageToDispatchWhileWaitingForSyncReply();

private:
    explicit SyncMessageState(SerialFunctionDispatcher& dispatcher)
        : m_dispatcher(dispatcher)
    {
    }
    static Lock syncMessageStateMapLock;
    // FIXME: Don't use raw pointers.
    static HashMap<SerialFunctionDispatcher*, SyncMessageState*>& syncMessageStateMap() WTF_REQUIRES_LOCK(syncMessageStateMapLock)
    {
        static NeverDestroyed<HashMap<SerialFunctionDispatcher*, SyncMessageState*>> map;
        return map;
    }

    BinarySemaphore m_waitForSyncReplySemaphore;

    // Protects m_didScheduleDispatchMessagesWorkSet and m_messagesToDispatchWhileWaitingForSyncReply.
    Lock m_lock;

    // The set of connections for which we've scheduled a call to dispatchMessageAndResetDidScheduleDispatchMessagesForConnection.
    HashSet<RefPtr<Connection>> m_didScheduleDispatchMessagesWorkSet WTF_GUARDED_BY_LOCK(m_lock);

    struct ConnectionAndIncomingMessage {
        Ref<Connection> connection;
        UniqueRef<Decoder> message;
        MessageIdentifier identifier { MessageIdentifier::generate() };

        void dispatch()
        {
            Ref { connection }->dispatchMessage(WTFMove(message));
        }
    };
    Deque<ConnectionAndIncomingMessage> m_messagesBeingDispatched; // Only used on the main thread.
    Deque<ConnectionAndIncomingMessage> m_messagesToDispatchWhileWaitingForSyncReply WTF_GUARDED_BY_LOCK(m_lock);

    ThreadSafeWeakPtr<SerialFunctionDispatcher> m_dispatcher;
    unsigned m_clients WTF_GUARDED_BY_LOCK(syncMessageStateMapLock) { 0 };
    friend struct Connection::SyncMessageStateRelease;
};

Lock Connection::SyncMessageState::syncMessageStateMapLock;

std::unique_ptr<Connection::SyncMessageState, Connection::SyncMessageStateRelease> Connection::SyncMessageState::get(SerialFunctionDispatcher& dispatcher)
{
    Locker locker { syncMessageStateMapLock };
    auto result = syncMessageStateMap().ensure(&dispatcher, [&dispatcher] { return new SyncMessageState { dispatcher }; }); // NOLINT.
    auto* state = result.iterator->value;
    state->m_clients++;
    return { state, Connection::SyncMessageStateRelease { } };
}

void Connection::SyncMessageStateRelease::operator()(SyncMessageState* instance) const
{
    if (!instance)
        return;
    {
        Locker locker { Connection::SyncMessageState::syncMessageStateMapLock };
        --instance->m_clients;
        if (instance->m_clients)
            return;
        if (RefPtr dispatcher = instance->dispatcher())
            Connection::SyncMessageState::syncMessageStateMap().remove(dispatcher.get());
    }
    delete instance;
}

void Connection::SyncMessageState::enqueueMatchingMessages(Connection& connection, MessageReceiveQueue& receiveQueue, const ReceiverMatcher& receiverMatcher)
{
    assertIsCurrent(*m_dispatcher.get());
    auto enqueueMatchingMessagesInContainer = [&](Deque<ConnectionAndIncomingMessage>& connectionAndMessages) {
        Deque<ConnectionAndIncomingMessage> rest;
        for (auto& connectionAndMessage : connectionAndMessages) {
            if (connectionAndMessage.connection.ptr() == &connection && connectionAndMessage.message->matches(receiverMatcher))
                receiveQueue.enqueueMessage(connection, WTFMove(connectionAndMessage.message));
            else
                rest.append(WTFMove(connectionAndMessage));
        }
        connectionAndMessages = WTFMove(rest);
    };
    Locker locker { m_lock };
    enqueueMatchingMessagesInContainer(m_messagesBeingDispatched);
    enqueueMatchingMessagesInContainer(m_messagesToDispatchWhileWaitingForSyncReply);
}

bool Connection::SyncMessageState::processIncomingMessage(Connection& connection, UniqueRef<Decoder>& message)
{
    switch (message->shouldDispatchMessageWhenWaitingForSyncReply()) {
    case ShouldDispatchWhenWaitingForSyncReply::No:
        return false;
    case ShouldDispatchWhenWaitingForSyncReply::YesDuringUnboundedIPC:
        if (!UnboundedSynchronousIPCScope::hasOngoingUnboundedSyncIPC())
            return false;
        break;
    case ShouldDispatchWhenWaitingForSyncReply::Yes:
        break;
    }

    bool shouldDispatch;
    {
        Locker locker { m_lock };
        shouldDispatch = m_didScheduleDispatchMessagesWorkSet.add(&connection).isNewEntry;
        connection.m_incomingMessagesLock.assertIsOwner();
        if (message->shouldMaintainOrderingWithAsyncMessages()) {
            // This sync message should maintain ordering with async messages so we need to process the pending async messages first.
            while (!connection.m_incomingMessages.isEmpty())
                m_messagesToDispatchWhileWaitingForSyncReply.append(ConnectionAndIncomingMessage { connection, connection.m_incomingMessages.takeFirst() });
        }
        m_messagesToDispatchWhileWaitingForSyncReply.append(ConnectionAndIncomingMessage { connection, WTFMove(message) });
    }

    if (shouldDispatch) {
        RefPtr dispatcher = m_dispatcher.get();
        RELEASE_ASSERT(dispatcher);
        dispatcher->dispatch([protectedConnection = Ref { connection }]() mutable {
            protectedConnection->dispatchSyncStateMessages();
        });
    }

    wakeUpClientRunLoop();

    return true;
}

void Connection::SyncMessageState::dispatchMessages(Function<void(MessageName, uint64_t)>&& willDispatchMessage)
{
    assertIsCurrent(*m_dispatcher.get());
    {
        Locker locker { m_lock };
        if (m_messagesBeingDispatched.isEmpty())
            m_messagesBeingDispatched = std::exchange(m_messagesToDispatchWhileWaitingForSyncReply, { });
        else {
            while (!m_messagesToDispatchWhileWaitingForSyncReply.isEmpty())
                m_messagesBeingDispatched.append(m_messagesToDispatchWhileWaitingForSyncReply.takeFirst());
        }
    }

    while (!m_messagesBeingDispatched.isEmpty()) {
        auto messageToDispatch = m_messagesBeingDispatched.takeFirst();
        if (willDispatchMessage)
            willDispatchMessage(messageToDispatch.message->messageName(), messageToDispatch.message->destinationID());
        messageToDispatch.dispatch();
    }
}

void Connection::SyncMessageState::dispatchMessagesUntil(MessageIdentifier lastMessageToDispatch)
{
    assertIsCurrent(*m_dispatcher.get());
    {
        Locker locker { m_lock };
        if (!m_messagesToDispatchWhileWaitingForSyncReply.containsIf([&](auto& message) { return message.identifier == lastMessageToDispatch; }))
            return; // This message has already been dispatched.

        while (!m_messagesToDispatchWhileWaitingForSyncReply.isEmpty()) {
            m_messagesBeingDispatched.append(m_messagesToDispatchWhileWaitingForSyncReply.takeFirst());
            if (m_messagesBeingDispatched.last().identifier == lastMessageToDispatch)
                break;
        }
    }

    while (!m_messagesBeingDispatched.isEmpty())
        m_messagesBeingDispatched.takeFirst().dispatch();
}

std::optional<MessageIdentifier> Connection::SyncMessageState::identifierOfLastMessageToDispatchWhileWaitingForSyncReply()
{
    Locker locker { m_lock };
    if (m_messagesToDispatchWhileWaitingForSyncReply.isEmpty())
        return std::nullopt;
    return m_messagesToDispatchWhileWaitingForSyncReply.last().identifier;
}

void Connection::SyncMessageState::dispatchMessagesAndResetDidScheduleDispatchMessagesForConnection(Connection& connection)
{
    assertIsCurrent(*m_dispatcher.get());
    {
        Locker locker { m_lock };
        ASSERT(m_didScheduleDispatchMessagesWorkSet.contains(&connection));
        m_didScheduleDispatchMessagesWorkSet.remove(&connection);
        Deque<ConnectionAndIncomingMessage> messagesToPutBack;
        for (auto& connectionAndIncomingMessage : m_messagesToDispatchWhileWaitingForSyncReply) {
            if (&connection == connectionAndIncomingMessage.connection.ptr())
                m_messagesBeingDispatched.append(WTFMove(connectionAndIncomingMessage));
            else
                messagesToPutBack.append(WTFMove(connectionAndIncomingMessage));
        }
        m_messagesToDispatchWhileWaitingForSyncReply = WTFMove(messagesToPutBack);
    }

    while (!m_messagesBeingDispatched.isEmpty())
        m_messagesBeingDispatched.takeFirst().dispatch(); // This may cause the function to re-enter when there is a nested run loop.
}

// Represents a sync request for which we're waiting on a reply.
struct Connection::PendingSyncReply {
    // The request ID.
    Markable<Connection::SyncRequestID> syncRequestID;

    // The reply decoder, will be null if there was an error processing the sync
    // message on the other side.
    std::unique_ptr<Decoder> replyDecoder;

    // To make sure we maintain message ordering, we keep track of the last message (that returns true for shouldDispatchMessageWhenWaitingForSyncReply())
    // and that was received *before* the sync reply. This is to make sure that we dispatch messages up until this one, before dispatching the sync reply.
    std::optional<MessageIdentifier> identifierOfLastMessageToDispatchBeforeSyncReply;

    PendingSyncReply() = default;

    explicit PendingSyncReply(Connection::SyncRequestID syncRequestID)
        : syncRequestID(syncRequestID)
    {
    }
};

Ref<Connection> Connection::createServerConnection(Identifier&& identifier, Thread::QOS receiveQueueQOS)
{
    return adoptRef(*new Connection(WTFMove(identifier), true, receiveQueueQOS));
}

Ref<Connection> Connection::createClientConnection(Identifier&& identifier)
{
    return adoptRef(*new Connection(WTFMove(identifier), false));
}

static HashMap<IPC::Connection::UniqueID, ThreadSafeWeakPtr<Connection>>& connectionMap() WTF_REQUIRES_LOCK(s_connectionMapLock)
{
    static NeverDestroyed<HashMap<IPC::Connection::UniqueID, ThreadSafeWeakPtr<Connection>>> map;
    return map;
}

Connection::Connection(Identifier&& identifier, bool isServer, Thread::QOS receiveQueueQOS)
    : m_uniqueID(UniqueID::generate())
    , m_isServer(isServer)
    , m_connectionQueue(WorkQueue::create("com.apple.IPC.ReceiveQueue"_s, receiveQueueQOS))
{
    {
        Locker locker { s_connectionMapLock };
        connectionMap().add(m_uniqueID, this);
    }

    platformInitialize(WTFMove(identifier));
}

Connection::~Connection()
{
    ASSERT(!isValid());

    {
        Locker locker { s_connectionMapLock };
        connectionMap().remove(m_uniqueID);
    }

    cancelAsyncReplyHandlers();
}

RefPtr<Connection> Connection::connection(UniqueID uniqueID)
{
    // FIXME(https://bugs.webkit.org/show_bug.cgi?id=238493): Removing with lock in destructor is not thread-safe.
    Locker locker { s_connectionMapLock };
    return connectionMap().get(uniqueID).get();
}

void Connection::setOnlySendMessagesAsDispatchWhenWaitingForSyncReplyWhenProcessingSuchAMessage(bool flag)
{
    ASSERT(!m_isConnected);

    m_onlySendMessagesAsDispatchWhenWaitingForSyncReplyWhenProcessingSuchAMessage = flag;
}

void Connection::setShouldExitOnSyncMessageSendFailure(bool shouldExitOnSyncMessageSendFailure)
{
    ASSERT(!m_isConnected);

    m_shouldExitOnSyncMessageSendFailure = shouldExitOnSyncMessageSendFailure;
}

// Enqueue any pending message to the MessageReceiveQueue that is meant to go on that queue. This is important to maintain the ordering of
// IPC messages as some messages may get received on the IPC thread before the message receiver registered itself on the main thread.
void Connection::enqueueMatchingMessagesToMessageReceiveQueue(MessageReceiveQueue& receiveQueue, const ReceiverMatcher& receiverMatcher)
{
    if (!isValid())
        return;
    // FIXME: m_isValid starts as true. It will be switched to start as false and toggled as true on
    // open. For the time being, check for m_syncState.
    if (m_syncState)
        m_syncState->enqueueMatchingMessages(*this, receiveQueue, receiverMatcher);

    Deque<UniqueRef<Decoder>> remainingIncomingMessages;
    for (auto& message : m_incomingMessages) {
        if (message->matches(receiverMatcher))
            receiveQueue.enqueueMessage(*this, WTFMove(message));
        else
            remainingIncomingMessages.append(WTFMove(message));
    }
    m_incomingMessages = WTFMove(remainingIncomingMessages);
}

void Connection::addMessageReceiveQueue(MessageReceiveQueue& receiveQueue, const ReceiverMatcher& receiverMatcher)
{
    Locker incomingMessagesLocker { m_incomingMessagesLock };
    enqueueMatchingMessagesToMessageReceiveQueue(receiveQueue, receiverMatcher);
    m_receiveQueues.add(receiveQueue, receiverMatcher);
}

void Connection::removeMessageReceiveQueue(const ReceiverMatcher& receiverMatcher)
{
    Locker locker { m_incomingMessagesLock };
    m_receiveQueues.remove(receiverMatcher);
}

void Connection::addWorkQueueMessageReceiver(ReceiverName receiverName, WorkQueue& workQueue, WorkQueueMessageReceiverBase& receiver, uint64_t destinationID)
{
    auto receiverMatcher = ReceiverMatcher::createWithZeroAsAnyDestination(receiverName, destinationID);

    auto receiveQueue = makeUnique<WorkQueueMessageReceiverQueue>(workQueue, receiver);
    Locker incomingMessagesLocker { m_incomingMessagesLock };
    enqueueMatchingMessagesToMessageReceiveQueue(*receiveQueue, receiverMatcher);
    m_receiveQueues.add(WTFMove(receiveQueue), receiverMatcher);
}

void Connection::removeWorkQueueMessageReceiver(ReceiverName receiverName, uint64_t destinationID)
{
    removeMessageReceiveQueue(ReceiverMatcher::createWithZeroAsAnyDestination(receiverName, destinationID));
}

void Connection::addMessageReceiver(FunctionDispatcher& dispatcher, MessageReceiver& receiver, ReceiverName receiverName, uint64_t destinationID)
{
    auto receiverMatcher = ReceiverMatcher::createWithZeroAsAnyDestination(receiverName, destinationID);
    auto receiveQueue = makeUnique<FunctionDispatcherQueue>(dispatcher, receiver);
    Locker incomingMessagesLocker { m_incomingMessagesLock };
    enqueueMatchingMessagesToMessageReceiveQueue(*receiveQueue, receiverMatcher);
    m_receiveQueues.add(WTFMove(receiveQueue), receiverMatcher);
}

void Connection::removeMessageReceiver(ReceiverName receiverName, uint64_t destinationID)
{
    removeMessageReceiveQueue(ReceiverMatcher::createWithZeroAsAnyDestination(receiverName, destinationID));
}

template<typename MessageReceiverType>
void Connection::dispatchMessageReceiverMessage(MessageReceiverType& messageReceiver, UniqueRef<Decoder>&& decoder)
{
#if ASSERT_ENABLED
    ++m_inDispatchMessageCount;
#endif

    if (decoder->isSyncMessage()) {
        auto replyEncoder = makeUniqueRef<Encoder>(MessageName::SyncMessageReply, decoder->syncRequestID().toUInt64());
        messageReceiver.didReceiveSyncMessage(*this, decoder.get(), replyEncoder);
        // If the message was not handled or handler tried to decode and marked it invalid, reply with
        // cancel message. For more info, see Connection:dispatchSyncMessage.
        std::unique_ptr remainingReplyEncoder = replyEncoder.moveToUniquePtr();
        if (remainingReplyEncoder)
            sendMessageImpl(makeUniqueRef<Encoder>(MessageName::CancelSyncMessageReply, decoder->syncRequestID().toUInt64()), { });
    } else
        messageReceiver.didReceiveMessage(*this, decoder.get());

#if ASSERT_ENABLED
    --m_inDispatchMessageCount;
#endif

#if ENABLE(IPC_TESTING_API)
    if (m_ignoreInvalidMessageForTesting)
        return;
#endif
    ASSERT(decoder->isValid());
    if (!decoder->isValid())
        dispatchDidReceiveInvalidMessage(decoder->messageName(), decoder->indexOfObjectFailingDecoding());
}

template void Connection::dispatchMessageReceiverMessage<MessageReceiver>(MessageReceiver&, UniqueRef<Decoder>&&);
template void Connection::dispatchMessageReceiverMessage<WorkQueueMessageReceiverBase>(WorkQueueMessageReceiverBase&, UniqueRef<Decoder>&&);

void Connection::setDidCloseOnConnectionWorkQueueCallback(DidCloseOnConnectionWorkQueueCallback callback)
{
    ASSERT(!m_isConnected);

    m_didCloseOnConnectionWorkQueueCallback = callback;
}

void Connection::setOutgoingMessageQueueIsGrowingLargeCallback(OutgoingMessageQueueIsGrowingLargeCallback&& callback)
{
    m_outgoingMessageQueueIsGrowingLargeCallback = WTFMove(callback);
}

bool Connection::open(Client& client, SerialFunctionDispatcher& dispatcher)
{
    ASSERT(!m_client);
    if (!platformPrepareForOpen())
        return false;
    m_client = &client;
    m_syncState = SyncMessageState::get(dispatcher);
    platformOpen();

    return true;
}

#if !USE(UNIX_DOMAIN_SOCKETS)
bool Connection::platformPrepareForOpen()
{
    return true;
}
#endif

Error Connection::flushSentMessages(Timeout timeout)
{
    Locker locker { m_outgoingMessagesLock };
    do {
        if (!isValid())
            return Error::InvalidConnection;
        if (m_outgoingMessages.isEmpty())
            return Error::NoError;
        m_outgoingMessagesEmptyCondition.waitUntil(m_outgoingMessagesLock, timeout.deadline());
    } while (!timeout.didTimeOut());
    return Error::Timeout;
}

void Connection::invalidate()
{
    m_isValid = false;
    if (!m_client)
        return;
    assertIsCurrent(dispatcher());
    m_client = nullptr;
    m_outgoingMessageQueueIsGrowingLargeCallback = nullptr;
    [this] {
        Locker locker { m_incomingMessagesLock };
        return WTFMove(m_syncState);
    }();

    cancelAsyncReplyHandlers();

    m_connectionQueue->dispatch([protectedThis = Ref { *this }]() mutable {
        protectedThis->platformInvalidate();
    });
}

auto Connection::createSyncMessageEncoder(MessageName messageName, uint64_t destinationID) -> std::pair<UniqueRef<Encoder>, SyncRequestID>
{
    auto encoder = makeUniqueRef<Encoder>(messageName, destinationID);

    // Encode the sync request ID.
    auto syncRequestID = makeSyncRequestID();
    encoder.get() << syncRequestID;

    return { WTFMove(encoder), syncRequestID };
}

#if ENABLE(CORE_IPC_SIGNPOSTS)

void* Connection::generateSignpostIdentifier()
{
    static std::atomic<uintptr_t> identifier;
    return reinterpret_cast<void*>(++identifier);
}

#endif

Error Connection::sendMessage(UniqueRef<Encoder>&& encoder, OptionSet<SendOption> sendOptions, std::optional<Thread::QOS> qos)
{
#if ENABLE(CORE_IPC_SIGNPOSTS)
    auto signpostIdentifier = generateSignpostIdentifier();
    WTFBeginSignpost(signpostIdentifier, IPCConnection, "sendMessage: %{public}s", description(encoder->messageName()).characters());
#endif

    auto error = sendMessageImpl(WTFMove(encoder), sendOptions, qos);

#if ENABLE(CORE_IPC_SIGNPOSTS)
    WTFEndSignpost(signpostIdentifier, IPCConnection);
#endif

    return error;
}

Error Connection::sendMessageImpl(UniqueRef<Encoder>&& encoder, OptionSet<SendOption> sendOptions, std::optional<Thread::QOS> qos)
{
    if (!isValid())
        return Error::InvalidConnection;

#if ENABLE(IPC_TESTING_API)
    if (isMainRunLoop()) {
        bool hasDeadObservers = false;
        for (auto& observerWeakPtr : m_messageObservers) {
            if (RefPtr observer = observerWeakPtr.get())
                observer->willSendMessage(encoder.get(), sendOptions);
            else
                hasDeadObservers = true;
        }
        if (hasDeadObservers)
            m_messageObservers.removeAllMatching([](auto& observer) { return !observer; });
    }
#endif

    if (isMainRunLoop() && m_inDispatchMessageMarkedToUseFullySynchronousModeForTesting && !encoder->isSyncMessage() && !(encoder->messageReceiverName() == ReceiverName::IPC)) {
        auto [wrappedMessage, syncRequestID] = createSyncMessageEncoder(MessageName::WrappedAsyncMessageForTesting, encoder->destinationID());
        wrappedMessage->setFullySynchronousModeForTesting();
        wrappedMessage->wrapForTesting(WTFMove(encoder));
        DecoderOrError result = sendSyncMessage(syncRequestID, WTFMove(wrappedMessage), Timeout::infinity(), { });
        return result.has_value() ? Error::NoError : result.error();
    }

#if ENABLE(IPC_TESTING_API)
    if (!sendOptions.contains(SendOption::IPCTestingMessage)) {
#endif
        if (sendOptions.contains(SendOption::DispatchMessageEvenWhenWaitingForSyncReply))
            ASSERT(encoder->isAllowedWhenWaitingForSyncReply());
        else if (sendOptions.contains(SendOption::DispatchMessageEvenWhenWaitingForUnboundedSyncReply))
            ASSERT(encoder->isAllowedWhenWaitingForUnboundedSyncReply());
        else if (encoder->messageName() != IPC::MessageName::WebPageProxy_HandleMessage) // HandleMessage is sent with and without DispatchMessageEvenWhenWaitingForSyncReply.
            ASSERT(!encoder->isAllowedWhenWaitingForSyncReply() && !encoder->isAllowedWhenWaitingForUnboundedSyncReply());
#if ENABLE(IPC_TESTING_API)
    }
#endif

    if (sendOptions.contains(SendOption::DispatchMessageEvenWhenWaitingForSyncReply)
        && (!m_onlySendMessagesAsDispatchWhenWaitingForSyncReplyWhenProcessingSuchAMessage
            || m_inDispatchMessageMarkedDispatchWhenWaitingForSyncReplyCount))
        encoder->setShouldDispatchMessageWhenWaitingForSyncReply(ShouldDispatchWhenWaitingForSyncReply::Yes);
    else if (sendOptions.contains(SendOption::DispatchMessageEvenWhenWaitingForUnboundedSyncReply))
        encoder->setShouldDispatchMessageWhenWaitingForSyncReply(ShouldDispatchWhenWaitingForSyncReply::YesDuringUnboundedIPC);

    bool shouldDispatchMessageSend;
    size_t outgoingMessagesCount;
    bool shouldNotifyOfQueueGrowingLarge;
    unsigned maxOutgoingMessageNameCount = 0;
    ASCIILiteral maxOutgoingMessageName;
    {
        Locker locker { m_outgoingMessagesLock };
        shouldDispatchMessageSend = m_outgoingMessages.isEmpty();
        m_outgoingMessages.append(WTFMove(encoder));
        outgoingMessagesCount = m_outgoingMessages.size();
        shouldNotifyOfQueueGrowingLarge = m_outgoingMessageQueueIsGrowingLargeCallback && outgoingMessagesCount > largeOutgoingMessageQueueCountThreshold && (MonotonicTime::now() - m_lastOutgoingMessageQueueIsGrowingLargeCallbackCallTime) >= largeOutgoingMessageQueueTimeThreshold;
        if (shouldNotifyOfQueueGrowingLarge) {
            HashCountedSet<ASCIILiteral> outgoingMessageNameCounts;
            for (auto& encoder : m_outgoingMessages) {
                auto name = description(encoder->messageName());
                auto result = outgoingMessageNameCounts.add(name);
                auto count = result.iterator->value;
                if (count > maxOutgoingMessageNameCount) {
                    maxOutgoingMessageNameCount = count;
                    maxOutgoingMessageName = name;
                }
            }
            m_lastOutgoingMessageQueueIsGrowingLargeCallbackCallTime = MonotonicTime::now();
        }
    }

    if (shouldNotifyOfQueueGrowingLarge) {
#if OS(DARWIN)
        RELEASE_LOG_ERROR(IPC, "Connection::sendMessage(): Too many messages (%zu) in the queue to remote PID: %d (most common: %u %" PUBLIC_LOG_STRING " messages), notifying client", outgoingMessagesCount, remoteProcessID(), maxOutgoingMessageNameCount, maxOutgoingMessageName.characters());
#else
        RELEASE_LOG_ERROR(IPC, "Connection::sendMessage(): Too many messages (%zu) in the queue, notifying client (most common: %u %" PUBLIC_LOG_STRING " messages)", outgoingMessagesCount, maxOutgoingMessageNameCount, maxOutgoingMessageName.characters());
#endif
        m_outgoingMessageQueueIsGrowingLargeCallback();
    }

    // It's not clear if calling dispatchWithQOS() will do anything if Connection::sendOutgoingMessages() is already running.
    if (shouldDispatchMessageSend || qos) {
        auto sendOutgoingMessages = [protectedThis = Ref { *this }]() mutable {
            protectedThis->sendOutgoingMessages();
        };

        if (qos)
            m_connectionQueue->dispatchWithQOS(WTFMove(sendOutgoingMessages), *qos);
        else
            m_connectionQueue->dispatch(WTFMove(sendOutgoingMessages));
    }

    return Error::NoError;
}

Error Connection::sendMessageWithAsyncReply(UniqueRef<Encoder>&& encoder, AsyncReplyHandler replyHandler, OptionSet<SendOption> sendOptions, std::optional<Thread::QOS> qos)
{
    ASSERT(replyHandler.replyID);
    ASSERT(replyHandler.completionHandler);
    auto replyID = *replyHandler.replyID;
    encoder.get() << replyID;

#if ENABLE(CORE_IPC_SIGNPOSTS)
    auto signpostIdentifier = generateSignpostIdentifier();
    replyHandler.completionHandler = CompletionHandler<void(Decoder*)>([signpostIdentifier, handler = WTFMove(replyHandler.completionHandler)](Decoder *decoder) mutable {
        WTFEndSignpost(signpostIdentifier, IPCConnection);
        handler(decoder);
    });

    WTFBeginSignpost(signpostIdentifier, IPCConnection, "sendMessageWithAsyncReply: %{public}s", description(encoder->messageName()).characters());
#endif

    addAsyncReplyHandler(WTFMove(replyHandler));

    auto error = sendMessageImpl(WTFMove(encoder), sendOptions, qos);
    if (error == Error::NoError)
        return Error::NoError;

    // replyHandlerToCancel might be already cancelled if invalidate() happened in-between.
    if (auto replyHandlerToCancel = takeAsyncReplyHandler(replyID)) {
        // FIXME: Current contract is that completionHandler is called on the connection run loop.
        // This does not make sense. However, this needs a change that is done later.
        RunLoop::mainSingleton().dispatch([completionHandler = WTFMove(replyHandlerToCancel)]() mutable {
            completionHandler(nullptr, nullptr);
        });
    }
    return error;
}

Error Connection::sendMessageWithAsyncReplyWithDispatcher(UniqueRef<Encoder>&& encoder, AsyncReplyHandlerWithDispatcher&& replyHandler, OptionSet<SendOption> sendOptions, std::optional<Thread::QOS> qos)
{
    ASSERT(replyHandler.replyID);
    ASSERT(replyHandler.completionHandler);
    auto replyID = *replyHandler.replyID;
    encoder.get() << replyID;
    addAsyncReplyHandlerWithDispatcher(WTFMove(replyHandler));
    auto error = sendMessage(WTFMove(encoder), sendOptions, qos);
    if (error == Error::NoError)
        return Error::NoError;

    if (auto replyHandlerToCancel = takeAsyncReplyHandlerWithDispatcher(replyID))
        replyHandlerToCancel(nullptr, nullptr);
    return error;
}

Error Connection::sendSyncReply(UniqueRef<Encoder>&& encoder)
{
    return sendMessageImpl(WTFMove(encoder), { });
}

Timeout Connection::timeoutRespectingIgnoreTimeoutsForTesting(Timeout timeout) const
{
    return m_ignoreTimeoutsForTesting ? Timeout::infinity() : timeout;
}

auto Connection::waitForMessage(MessageName messageName, uint64_t destinationID, Timeout timeout, OptionSet<WaitForOption> waitForOptions) -> DecoderOrError
{
    if (!isValid())
        return makeUnexpected(Error::InvalidConnection);

#if ENABLE(CORE_IPC_SIGNPOSTS)
    auto signpostIdentifier = generateSignpostIdentifier();
    WTFBeginSignpost(signpostIdentifier, IPCConnection, "waitForMessage: %{public}s", description(messageName).characters());
    auto endSignpost = makeScopeExit([&] {
        WTFEndSignpost(signpostIdentifier, IPCConnection);
    });
#endif

    assertIsCurrent(dispatcher());
    Ref protectedThis { *this };

    timeout = timeoutRespectingIgnoreTimeoutsForTesting(timeout);

    WaitForMessageState waitingForMessage(messageName, destinationID, waitForOptions);

    {
        Locker locker { m_waitForMessageLock };

        // We don't support having multiple clients waiting for messages.
        ASSERT(!m_waitingForMessage);
        if (m_waitingForMessage)
            return makeUnexpected(Error::MultipleWaitingClients);

        // If the connection is already invalidated, don't even start waiting.
        // Once m_waitingForMessage is set, messageWaitingInterrupted will cover this instead.
        if (!m_shouldWaitForMessages)
            return makeUnexpected(Error::AttemptingToWaitOnClosedConnection);

        bool hasIncomingSynchronousMessage = false;

        // First, check if this message is already in the incoming messages queue.
        {
            Locker locker { m_incomingMessagesLock };
            for (auto it = m_incomingMessages.begin(), end = m_incomingMessages.end(); it != end; ++it) {
                UniqueRef<Decoder>& message = *it;

                if (message->messageName() == messageName && message->destinationID() == destinationID) {
                    UniqueRef<Decoder> returnedMessage = WTFMove(message);

                    m_incomingMessages.remove(it);
                    return { WTFMove(returnedMessage) };
                }

                if (message->isSyncMessage())
                    hasIncomingSynchronousMessage = true;
            }
        }

        // Don't even start waiting if we have InterruptWaitingIfSyncMessageArrives and there's a sync message already in the queue.
        if (hasIncomingSynchronousMessage && waitForOptions.contains(WaitForOption::InterruptWaitingIfSyncMessageArrives))
            return makeUnexpected(Error::SyncMessageInterruptedWait);

        m_waitingForMessage = &waitingForMessage;
    }

    // Now wait for it to be set.
    while (true) {
        // Handle any messages that are blocked on a response from us.
        bool wasMessageToWaitForAlreadyDispatched = false;
        m_syncState->dispatchMessages([&](auto nameOfMessageToDispatch, uint64_t destinationOfMessageToDispatch) {
            wasMessageToWaitForAlreadyDispatched |= messageName == nameOfMessageToDispatch && destinationID == destinationOfMessageToDispatch;
        });

        Locker locker { m_waitForMessageLock };

        if (wasMessageToWaitForAlreadyDispatched) {
            m_waitingForMessage = nullptr;
            return makeUnexpected(Error::WaitingOnAlreadyDispatchedMessage);
        }

        if (m_inDispatchSyncMessageCount && !timeout.isInfinity()) [[unlikely]] {
            RELEASE_LOG_ERROR(IPC, "Connection::waitForMessage(%" PUBLIC_LOG_STRING "): Exiting immediately, since we're handling a sync message already", description(messageName).characters());
            m_waitingForMessage = nullptr;
            return makeUnexpected(Error::AttemptingToWaitInsideSyncMessageHandling);
        }

        if (m_waitingForMessage->decoder) {
            auto decoder = makeUniqueRefFromNonNullUniquePtr(WTFMove(m_waitingForMessage->decoder));
            m_waitingForMessage = nullptr;
            return { WTFMove(decoder) };
        }

        if (!isValid()) {
            m_waitingForMessage = nullptr;
            return makeUnexpected(Error::InvalidConnection);
        }

        bool didTimeout = !m_waitForMessageCondition.waitUntil(m_waitForMessageLock, timeout.deadline());
        if (didTimeout) {
            m_waitingForMessage = nullptr;
            return makeUnexpected(Error::Timeout);
        }
        if (m_waitingForMessage->messageWaitingInterrupted) {
            m_waitingForMessage = nullptr;
            if (m_shouldWaitForMessages)
                return makeUnexpected(Error::SyncMessageInterruptedWait);
            return makeUnexpected(Error::AttemptingToWaitOnClosedConnection);
        }
    }

    return makeUnexpected(Error::Unspecified);
}

bool Connection::pushPendingSyncRequestID(SyncRequestID syncRequestID)
{
    {
        Locker locker { m_syncReplyStateLock };
        if (!m_shouldWaitForSyncReplies)
            return false;
        m_pendingSyncReplies.append(PendingSyncReply(syncRequestID));
    }
    ++m_inSendSyncCount;
    return true;
}

void Connection::popPendingSyncRequestID(SyncRequestID syncRequestID)
{
    --m_inSendSyncCount;
    Locker locker { m_syncReplyStateLock };
    ASSERT_UNUSED(syncRequestID, m_pendingSyncReplies.last().syncRequestID == syncRequestID);
    m_pendingSyncReplies.removeLast();
}

auto Connection::sendSyncMessage(SyncRequestID syncRequestID, UniqueRef<Encoder>&& encoder, Timeout timeout, OptionSet<SendSyncOption> sendSyncOptions) -> DecoderOrError
{
    if (!isValid()) {
        didFailToSendSyncMessage(Error::InvalidConnection);
        return makeUnexpected(Error::InvalidConnection);
    }
    assertIsCurrent(dispatcher());
    if (!pushPendingSyncRequestID(syncRequestID)) {
        didFailToSendSyncMessage(Error::CantWaitForSyncReplies);
        return makeUnexpected(Error::CantWaitForSyncReplies);
    }

    // First send the message.
    OptionSet<SendOption> sendOptions = IPC::SendOption::DispatchMessageEvenWhenWaitingForSyncReply;
    if (sendSyncOptions.contains(SendSyncOption::ForceDispatchWhenDestinationIsWaitingForUnboundedSyncReply))
        sendOptions = sendOptions | IPC::SendOption::DispatchMessageEvenWhenWaitingForUnboundedSyncReply;

    if (sendSyncOptions.contains(IPC::SendSyncOption::MaintainOrderingWithAsyncMessages))
        encoder->setShouldMaintainOrderingWithAsyncMessages();

    auto messageName = encoder->messageName();

#if ENABLE(CORE_IPC_SIGNPOSTS)
    auto signpostIdentifier = generateSignpostIdentifier();
    WTFBeginSignpost(signpostIdentifier, IPCConnection, "sendSyncMessage: %{public}s", description(messageName).characters());
#endif

    // Since sync IPC is blocking the current thread, make sure we use the same priority for the IPC sending thread
    // as the current thread.
    sendMessageImpl(WTFMove(encoder), sendOptions, Thread::currentThreadQOS());

    // Then wait for a reply. Waiting for a reply could involve dispatching incoming sync messages, so
    // keep an extra reference to the connection here in case it's invalidated.
    Ref<Connection> protect(*this);
    auto replyOrError = waitForSyncReply(syncRequestID, messageName, timeout, sendSyncOptions);

#if ENABLE(CORE_IPC_SIGNPOSTS)
    WTFEndSignpost(signpostIdentifier, IPCConnection);
#endif

    popPendingSyncRequestID(syncRequestID);

    if (!replyOrError.has_value()) {
        if (replyOrError.error() == Error::NoError)
            replyOrError = makeUnexpected(Error::Unspecified);
        didFailToSendSyncMessage(replyOrError.error());
    }

    return replyOrError;
}

auto Connection::waitForSyncReply(SyncRequestID syncRequestID, MessageName messageName, Timeout timeout, OptionSet<SendSyncOption> sendSyncOptions) -> DecoderOrError
{
    timeout = timeoutRespectingIgnoreTimeoutsForTesting(timeout);

    bool timedOut = false;
    while (!timedOut) {
        // First, check if we have any messages that we need to process.
        m_syncState->dispatchMessages();

        {
            Locker locker { m_syncReplyStateLock };

            // Second, check if there is a sync reply at the top of the stack.
            ASSERT(!m_pendingSyncReplies.isEmpty());

            auto& pendingSyncReply = m_pendingSyncReplies.last();
            ASSERT_UNUSED(syncRequestID, pendingSyncReply.syncRequestID == syncRequestID);

            // We found the sync reply.
            if (pendingSyncReply.replyDecoder) {
                auto replyDecoder = std::exchange(pendingSyncReply.replyDecoder, nullptr);
                if (auto identifierOfLastMessageToDispatchBeforeSyncReply = pendingSyncReply.identifierOfLastMessageToDispatchBeforeSyncReply) {
                    locker.unlockEarly();

                    // Dispatch messages (that return true for shouldDispatchMessageWhenWaitingForSyncReply()) that
                    // were received before this sync reply, in order to maintain ordering.
                    m_syncState->dispatchMessagesUntil(*identifierOfLastMessageToDispatchBeforeSyncReply);
                }

                return makeUniqueRefFromNonNullUniquePtr(WTFMove(replyDecoder));
            }

            // The connection was closed.
            if (!m_shouldWaitForSyncReplies)
                return makeUnexpected(Error::InvalidConnection);
        }

        // Processing a sync message could cause the connection to be invalidated.
        // (If the handler ends up calling Connection::invalidate).
        // If that happens, we need to stop waiting, or we'll hang since we won't get
        // any more incoming messages.
        if (!isValid()) {
            RELEASE_LOG_ERROR(IPC, "Connection::waitForSyncReply: Connection no longer valid, id=%" PRIu64, syncRequestID.toUInt64());
            return makeUnexpected(Error::InvalidConnection);
        }

        // We didn't find a sync reply yet, keep waiting.
        // This allows the WebProcess to still serve clients while waiting for the message to return.
        // Notably, it can continue to process accessibility requests, which are on the main thread.
        timedOut = !m_syncState->wait(timeout);
    }

#if OS(DARWIN)
    RELEASE_LOG_ERROR(IPC, "Connection::waitForSyncReply: Timed-out while waiting for reply for %" PUBLIC_LOG_STRING " from process %d, id=%" PRIu64, description(messageName).characters(), remoteProcessID(), syncRequestID.toUInt64());
#else
    RELEASE_LOG_ERROR(IPC, "Connection::waitForSyncReply: Timed-out while waiting for reply for %s, id=%" PRIu64, description(messageName).characters(), syncRequestID.toUInt64());
#endif

    return makeUnexpected(Error::Timeout);
}

void Connection::processIncomingSyncReply(UniqueRef<Decoder> decoder)
{
    {
        Locker locker { m_syncReplyStateLock };

        // Go through the stack of sync requests that have pending replies and see which one
        // this reply is for.
        for (size_t i = m_pendingSyncReplies.size(); i > 0; --i) {
            PendingSyncReply& pendingSyncReply = m_pendingSyncReplies[i - 1];

            if (pendingSyncReply.syncRequestID->toUInt64() != decoder->destinationID())
                continue;

            ASSERT(!pendingSyncReply.replyDecoder);

            pendingSyncReply.replyDecoder = decoder.moveToUniquePtr();

            // Keep track of the last message (that returns true for shouldDispatchMessageWhenWaitingForSyncReply())
            // we've received before this sync reply. This is to make sure that we dispatch all messages up to this
            // one, before the sync reply, to maintain ordering.
            pendingSyncReply.identifierOfLastMessageToDispatchBeforeSyncReply = m_syncState->identifierOfLastMessageToDispatchWhileWaitingForSyncReply();

            // We got a reply to the last send message, wake up the client run loop so it can be processed.
            if (i == m_pendingSyncReplies.size()) {
                Locker locker { m_incomingMessagesLock };
                if (m_syncState)
                    m_syncState->wakeUpClientRunLoop();
            }
            return;
        }
    }

    // If we get here, it means we got a reply for a message that wasn't in the sync request stack or map.
    // This can happen if the send timed out, so it's fine to ignore.
}

void Connection::processIncomingMessage(UniqueRef<Decoder> message)
{
    ASSERT(message->messageReceiverName() != ReceiverName::Invalid);

    if (!message->isValid()) {
        // If the message is invalid, we could send back a SyncMessageError. In case the message
        // would need a reply, we do not cancel it as we don't know the destination to cancel it
        // with. Currently ther is no use-case to handle invalid messages.
        dispatchDidReceiveInvalidMessage(message->messageName(), message->indexOfObjectFailingDecoding());
        return;
    }

    if (message->messageName() == MessageName::SyncMessageReply || message->messageName() == MessageName::CancelSyncMessageReply) {
        processIncomingSyncReply(WTFMove(message));
        return;
    }

    if (!MessageReceiveQueueMap::isValidMessage(message.get())) {
        dispatchDidReceiveInvalidMessage(message->messageName(), message->indexOfObjectFailingDecoding());
        return;
    }

    // FIXME: These are practically the same mutex, so maybe they could be merged.
    Locker waitForMessagesLocker { m_waitForMessageLock };

    Locker incomingMessagesLocker { m_incomingMessagesLock };
    if (!m_syncState)
        return;

    if (message->messageReceiverName() == ReceiverName::AsyncReply) {
        if (auto replyHandlerWithDispatcher = takeAsyncReplyHandlerWithDispatcherWithLockHeld(AtomicObjectIdentifier<AsyncReplyIDType>(message->destinationID()))) {
            replyHandlerWithDispatcher(this, message.moveToUniquePtr());
            return;
        }
        // Fallback to default case, error handling will be performed in sendMessage().
    }

    if (auto* receiveQueue = m_receiveQueues.get(message.get())) {
        receiveQueue->enqueueMessage(*this, WTFMove(message));
        return;
    }

    if (message->isSyncMessage()) {
        Locker locker { m_incomingSyncMessageCallbackLock };

        for (auto& callback : m_incomingSyncMessageCallbacks.values())
            RefPtr { m_incomingSyncMessageCallbackQueue }->dispatch(WTFMove(callback));

        m_incomingSyncMessageCallbacks.clear();
    }

    // Check if we're waiting for this message, or if we need to interrupt waiting due to an incoming sync message.
    if (m_waitingForMessage && !m_waitingForMessage->decoder) {
        if (m_waitingForMessage->messageName == message->messageName() && m_waitingForMessage->destinationID == message->destinationID()) {
            m_waitingForMessage->decoder = message.moveToUniquePtr();
            ASSERT(m_waitingForMessage->decoder);
            m_waitForMessageCondition.notifyOne();
            return;
        }

        if (m_waitingForMessage->waitForOptions.contains(WaitForOption::DispatchIncomingSyncMessagesWhileWaiting) && message->isSyncMessage() && m_syncState->processIncomingMessage(*this, message)) {
            m_waitForMessageCondition.notifyOne();
            return;
        }

        if (m_waitingForMessage->waitForOptions.contains(WaitForOption::InterruptWaitingIfSyncMessageArrives) && message->isSyncMessage()) {
            m_waitingForMessage->messageWaitingInterrupted = true;
            m_waitForMessageCondition.notifyOne();
            enqueueIncomingMessage(WTFMove(message));
            return;
        }
    }

    if ((message->shouldDispatchMessageWhenWaitingForSyncReply() == ShouldDispatchWhenWaitingForSyncReply::YesDuringUnboundedIPC && !message->isAllowedWhenWaitingForUnboundedSyncReply()) || (message->shouldDispatchMessageWhenWaitingForSyncReply() == ShouldDispatchWhenWaitingForSyncReply::Yes && !message->isAllowedWhenWaitingForSyncReply())) {
        dispatchDidReceiveInvalidMessage(message->messageName(), message->indexOfObjectFailingDecoding());
        return;
    }

    // Check if this is a sync message or if it's a message that should be dispatched even when waiting for
    // a sync reply. If it is, and we're waiting for a sync reply this message needs to be dispatched.
    // If we don't we'll end up with a deadlock where both sync message senders are stuck waiting for a reply.
    if (m_syncState->processIncomingMessage(*this, message))
        return;

    enqueueIncomingMessage(WTFMove(message));
}

uint64_t Connection::installIncomingSyncMessageCallback(WTF::Function<void ()>&& callback)
{
    Locker locker { m_incomingSyncMessageCallbackLock };

    m_nextIncomingSyncMessageCallbackID++;

    if (!m_incomingSyncMessageCallbackQueue)
        m_incomingSyncMessageCallbackQueue = WorkQueue::create("com.apple.WebKit.IPC.IncomingSyncMessageCallbackQueue"_s);

    m_incomingSyncMessageCallbacks.add(m_nextIncomingSyncMessageCallbackID, WTFMove(callback));

    return m_nextIncomingSyncMessageCallbackID;
}

void Connection::uninstallIncomingSyncMessageCallback(uint64_t callbackID)
{
    Locker locker { m_incomingSyncMessageCallbackLock };
    m_incomingSyncMessageCallbacks.remove(callbackID);
}

bool Connection::hasIncomingSyncMessage()
{
    Locker locker { m_incomingMessagesLock };

    for (auto& message : m_incomingMessages) {
        if (message->isSyncMessage())
            return true;
    }

    return false;
}

void Connection::enableIncomingMessagesThrottling()
{
    if (isIncomingMessagesThrottlingEnabled())
        return;
    m_incomingMessagesThrottlingLevel = 0;
}

#if ENABLE(IPC_TESTING_API)
void Connection::addMessageObserver(const MessageObserver& observer)
{
    m_messageObservers.append(observer);
}

void Connection::dispatchIncomingMessageForTesting(UniqueRef<Decoder>&& decoder)
{
    m_connectionQueue->dispatch([protectedThis = Ref { *this }, decoder = WTFMove(decoder)]() mutable {
        protectedThis->processIncomingMessage(WTFMove(decoder));
    });
}
#endif

void Connection::connectionDidClose()
{
    // The connection is now invalid.
    m_isValid = false;
    platformInvalidate();

    bool hasPendingWaiters = false;
    {
        Locker locker { m_syncReplyStateLock };

        ASSERT(m_shouldWaitForSyncReplies);
        m_shouldWaitForSyncReplies = false;

        hasPendingWaiters = !m_pendingSyncReplies.isEmpty();
    }

    if (hasPendingWaiters) {
        Locker locker { m_incomingMessagesLock };
        if (m_syncState)
            m_syncState->wakeUpClientRunLoop();
    }

    {
        Locker locker { m_waitForMessageLock };

        ASSERT(m_shouldWaitForMessages);
        m_shouldWaitForMessages = false;

        if (m_waitingForMessage)
            m_waitingForMessage->messageWaitingInterrupted = true;
    }
    m_waitForMessageCondition.notifyAll();

    {
        Locker locker { m_outgoingMessagesLock };
        m_outgoingMessages.clear();
        m_outgoingMessagesEmptyCondition.notifyAll();
    }

    if (m_didCloseOnConnectionWorkQueueCallback)
        m_didCloseOnConnectionWorkQueueCallback(this);

    dispatchDidCloseAndInvalidate();
}

bool Connection::canSendOutgoingMessages() const
{
    return m_isConnected && platformCanSendOutgoingMessages();
}

void Connection::sendOutgoingMessages()
{
    if (!canSendOutgoingMessages())
        return;

    while (true) {
        std::unique_ptr<Encoder> message;

        {
            Locker locker { m_outgoingMessagesLock };
            if (m_outgoingMessages.isEmpty()) {
                m_outgoingMessagesEmptyCondition.notifyAll();
                break;
            }
            message = m_outgoingMessages.takeFirst().moveToUniquePtr();
        }
        ASSERT(message);

        if (!sendOutgoingMessage(makeUniqueRefFromNonNullUniquePtr(WTFMove(message))))
            break;
    }
}

void Connection::dispatchSyncMessage(Decoder& decoder)
{
    assertIsCurrent(dispatcher());
    ASSERT(decoder.isSyncMessage());

    ++m_inDispatchSyncMessageCount;
    auto decrementSyncMessageCount = makeScopeExit([&] {
        ASSERT(m_inDispatchSyncMessageCount);
        --m_inDispatchSyncMessageCount;
    });

    UniqueRef replyEncoder = makeUniqueRef<Encoder>(MessageName::SyncMessageReply, decoder.syncRequestID().toUInt64());
    if (decoder.messageName() == MessageName::WrappedAsyncMessageForTesting) {
        if (m_fullySynchronousModeIsAllowedForTesting) {
            std::unique_ptr<Decoder> unwrappedDecoder = Decoder::unwrapForTesting(decoder);
            RELEASE_ASSERT(unwrappedDecoder);
            processIncomingMessage(makeUniqueRefFromNonNullUniquePtr(WTFMove(unwrappedDecoder)));
            m_syncState->dispatchMessages();
            sendMessageImpl(WTFMove(replyEncoder), { });
        } else
            decoder.markInvalid();
    } else
        protectedClient()->didReceiveSyncMessage(*this, decoder, replyEncoder);

    // If the message was not handled, i.e. replyEncoder was not consumed, reply with cancel
    // message. We do not distinquish between a decode failure and failure to find a
    // destination, as there would be an observable difference when sending malformed message to
    // non-existing id (cancel) and sending malformed message to existing id (decoding error).
    // FIXME: Using potentially moved-from UniquePtr is not nice but at the moment well-defined.
    // In later work, the encoder will be removed altogether.
    std::unique_ptr remainingReplyEncoder = replyEncoder.moveToUniquePtr();
    if (remainingReplyEncoder)
        sendMessageImpl(makeUniqueRef<Encoder>(MessageName::CancelSyncMessageReply, decoder.syncRequestID().toUInt64()), { });
}

void Connection::dispatchDidReceiveInvalidMessage(MessageName messageName, int32_t indexOfObjectFailingDecoding)
{
    dispatchToClient([protectedThis = Ref { *this }, messageName, indexOfObjectFailingDecoding] {
        if (!protectedThis->isValid())
            return;
        protectedThis->protectedClient()->didReceiveInvalidMessage(protectedThis, messageName, indexOfObjectFailingDecoding);
    });
}

void Connection::dispatchDidCloseAndInvalidate()
{
    dispatchToClient([protectedThis = Ref { *this }] {
        // If the connection has been explicitly invalidated before dispatchConnectionDidClose was called,
        // then the connection client will be nullptr here.
        RefPtr client = protectedThis->m_client.get();
        if (!client)
            return;
        client->didClose(protectedThis);
        protectedThis->invalidate();
    });
}

size_t Connection::pendingMessageCountForTesting() const
{
    // Note: current testing does not need to inspect the sync message state.
    Locker lock { m_incomingMessagesLock };
    return m_incomingMessages.size();
}

void Connection::dispatchOnReceiveQueueForTesting(Function<void()>&& completionHandler)
{
    m_connectionQueue->dispatch(WTFMove(completionHandler));
}

void Connection::didFailToSendSyncMessage(Error)
{
    if (!m_shouldExitOnSyncMessageSendFailure)
        return;

    exitProcess(0);
}

void Connection::enqueueIncomingMessage(UniqueRef<Decoder> incomingMessage)
{
    m_incomingMessagesLock.assertIsOwner();
    {
#if PLATFORM(COCOA)
        if (m_didRequestProcessTermination)
            return;

        if (isIncomingMessagesThrottlingEnabled() && m_incomingMessages.size() >= maxPendingIncomingMessagesKillingThreshold) {
            m_didRequestProcessTermination = true;
            dispatchToClientWithIncomingMessagesLock([protectedThis = Ref { *this }] {
                RefPtr client = protectedThis->m_client.get();
                if (!client)
                    return;
                client->requestRemoteProcessTermination();
                RELEASE_LOG_FAULT(IPC, "%p - Connection::enqueueIncomingMessage: Over %zu incoming messages have been queued without the main thread processing them, terminating the remote process as it seems to be misbehaving", protectedThis.ptr(), maxPendingIncomingMessagesKillingThreshold);
                Locker lock { protectedThis->m_incomingMessagesLock };
                protectedThis->m_incomingMessages.clear();
            });
            return;
        }
#endif

        m_incomingMessages.append(WTFMove(incomingMessage));

        if (isIncomingMessagesThrottlingEnabled() && m_incomingMessages.size() != 1)
            return;
    }

    if (!m_syncState)
        return;
    if (isIncomingMessagesThrottlingEnabled()) {
        dispatcher().dispatch([protectedThis = Ref { *this }] {
            protectedThis->dispatchIncomingMessages();
        });
    } else {
        dispatcher().dispatch([protectedThis = Ref { *this }] {
            protectedThis->dispatchOneIncomingMessage();
        });
    }
}

void Connection::dispatchMessage(Decoder& decoder)
{
    assertIsCurrent(dispatcher());
    RefPtr client = m_client.get();
    RELEASE_ASSERT(client);
    if (decoder.messageReceiverName() == ReceiverName::AsyncReply) {
        auto handler = takeAsyncReplyHandler(AtomicObjectIdentifier<AsyncReplyIDType>(decoder.destinationID()));
        if (!handler) {
            markCurrentlyDispatchedMessageAsInvalid();
#if ENABLE(IPC_TESTING_API)
            if (m_ignoreInvalidMessageForTesting)
                return;
#endif
            ASSERT_NOT_REACHED();
            return;
        }
        handler(this, &decoder);
        return;
    }

#if ENABLE(IPC_TESTING_API)
    if (isMainRunLoop()) {
        bool hasDeadObservers = false;
        for (auto& observerWeakPtr : m_messageObservers) {
            if (RefPtr observer = observerWeakPtr.get())
                observer->didReceiveMessage(decoder);
            else
                hasDeadObservers = true;
        }
        if (hasDeadObservers)
            m_messageObservers.removeAllMatching([](auto& observer) { return !observer; });
    }
#endif

    client->didReceiveMessage(*this, decoder);
}

void Connection::dispatchMessage(UniqueRef<Decoder> message)
{
    if (!m_syncState)
        return;
    assertIsCurrent(dispatcher());
    {
        // FIXME: The matches here come from
        // m_messagesToDispatchWhileWaitingForSyncReply. This causes message
        // reordering, because some of the messages go to
        // SyncState::m_messagesToDispatchWhileWaitingForSyncReply while others
        // go to Connection::m_incomingMessages. Should be fixed by adding all
        // messages to one list.
        Locker incomingMessagesLocker { m_incomingMessagesLock };
        if (auto* receiveQueue = m_receiveQueues.get(message.get())) {
            receiveQueue->enqueueMessage(*this, WTFMove(message));
            return;
        }
    }

    if (message->shouldUseFullySynchronousModeForTesting()) {
        if (!m_fullySynchronousModeIsAllowedForTesting) {
#if ENABLE(IPC_TESTING_API)
            if (m_ignoreInvalidMessageForTesting)
                return;
#endif
            protectedClient()->didReceiveInvalidMessage(*this, message->messageName(), message->indexOfObjectFailingDecoding());
            return;
        }
        m_inDispatchMessageMarkedToUseFullySynchronousModeForTesting++;
    }

#if ASSERT_ENABLED
    ++m_inDispatchMessageCount;
#endif

    bool isDispatchingMessageWhileWaitingForSyncReply = (message->shouldDispatchMessageWhenWaitingForSyncReply() == ShouldDispatchWhenWaitingForSyncReply::Yes)
        || (message->shouldDispatchMessageWhenWaitingForSyncReply() == ShouldDispatchWhenWaitingForSyncReply::YesDuringUnboundedIPC && UnboundedSynchronousIPCScope::hasOngoingUnboundedSyncIPC());

    if (isDispatchingMessageWhileWaitingForSyncReply)
        m_inDispatchMessageMarkedDispatchWhenWaitingForSyncReplyCount++;

    bool oldDidReceiveInvalidMessage = m_didReceiveInvalidMessage;
    m_didReceiveInvalidMessage = false;

    if (message->isSyncMessage())
        dispatchSyncMessage(message.get());
    else
        dispatchMessage(message.get());

    m_didReceiveInvalidMessage |= !message->isValid();

#if ASSERT_ENABLED
    --m_inDispatchMessageCount;
#endif

    // FIXME: For synchronous messages, we should not decrement the counter until we send a response.
    // Otherwise, we would deadlock if processing the message results in a sync message back after we exit this function.
    if (isDispatchingMessageWhileWaitingForSyncReply)
        m_inDispatchMessageMarkedDispatchWhenWaitingForSyncReplyCount--;

    if (message->shouldUseFullySynchronousModeForTesting())
        m_inDispatchMessageMarkedToUseFullySynchronousModeForTesting--;

    bool didReceiveInvalidMessage = m_didReceiveInvalidMessage;
    m_didReceiveInvalidMessage = oldDidReceiveInvalidMessage;

#if ENABLE(IPC_TESTING_API)
    if (m_ignoreInvalidMessageForTesting)
        return;
#endif
#if ASSERT_ENABLED
    if (didReceiveInvalidMessage) {
        WTFLogAlways("Received invalid message %s for destination %" PRIu64, description(message->messageName()).characters(), message->destinationID());
        ASSERT_NOT_REACHED();
    }
#endif
    if (didReceiveInvalidMessage && isValid())
        protectedClient()->didReceiveInvalidMessage(*this, message->messageName(), message->indexOfObjectFailingDecoding());
}

size_t Connection::numberOfMessagesToProcess(size_t totalMessages)
{
    // Never dispatch more than 600 messages without returning to the run loop, we can go as low as 60 with maximum throttling level.
    static const size_t maxIncomingMessagesDispatchingBatchSize { 600 };
    static const uint8_t maxThrottlingLevel = 9;

    size_t batchSize = maxIncomingMessagesDispatchingBatchSize / (*m_incomingMessagesThrottlingLevel + 1);

    if (totalMessages > maxIncomingMessagesDispatchingBatchSize)
        m_incomingMessagesThrottlingLevel = std::min<uint8_t>(*m_incomingMessagesThrottlingLevel + 1u, maxThrottlingLevel);
    else if (*m_incomingMessagesThrottlingLevel)
        --*m_incomingMessagesThrottlingLevel;

    return std::min(totalMessages, batchSize);
}

SerialFunctionDispatcher& Connection::dispatcher()
{
    // dispatcher can only be accessed while the connection is valid,
    // and must have the incoming message lock held if not being
    // called from the SerialFunctionDispatcher.
    RELEASE_ASSERT(m_syncState);
    RefPtr dispatcher = m_syncState->dispatcher();
    RELEASE_ASSERT(dispatcher);
#if !ENABLE(UNFAIR_LOCK)
    if (!m_incomingMessagesLock.isLocked())
        assertIsCurrent(*dispatcher);
#endif

    // Our syncState is specific to the SerialFunctionDispatcher we have been
    // bound to during open(), so we can retrieve the SerialFunctionDispatcher
    // from it (rather than storing another pointer on this class).
    return *dispatcher; // FIXME: This is unsafe. This function should return RefPtr instead.
}

void Connection::dispatchOneIncomingMessage()
{
    std::unique_ptr<Decoder> message;
    {
        Locker locker { m_incomingMessagesLock };
        if (m_incomingMessages.isEmpty())
            return;

        message = m_incomingMessages.takeFirst().moveToUniquePtr();
    }

    dispatchMessage(makeUniqueRefFromNonNullUniquePtr(WTFMove(message)));
}

void Connection::dispatchSyncStateMessages()
{
    if (m_syncState) {
        assertIsCurrent(dispatcher());
        m_syncState->dispatchMessagesAndResetDidScheduleDispatchMessagesForConnection(*this);
    }
}

void Connection::dispatchIncomingMessages()
{
    if (!isValid())
        return;

    std::unique_ptr<Decoder> message;

    size_t messagesToProcess = 0;
    {
        Locker locker { m_incomingMessagesLock };
        if (m_incomingMessages.isEmpty())
            return;

        message = m_incomingMessages.takeFirst().moveToUniquePtr();

        // Incoming messages may get adding to the queue by the IPC thread while we're dispatching the messages below.
        // To make sure dispatchIncomingMessages() yields, we only ever process messages that were in the queue when
        // dispatchIncomingMessages() was called. Additionally, the message throttling may further cap the number of
        // messages to process to make sure we give the main run loop a chance to process other events.
        messagesToProcess = numberOfMessagesToProcess(m_incomingMessages.size());
        if (messagesToProcess < m_incomingMessages.size()) {
            RELEASE_LOG_ERROR(IPC, "%p - Connection::dispatchIncomingMessages: IPC throttling was triggered (has %zu pending incoming messages, will only process %zu before yielding)", this, m_incomingMessages.size(), messagesToProcess);
            RELEASE_LOG_ERROR(IPC, "%p - Connection::dispatchIncomingMessages: first IPC message in queue is %" PUBLIC_LOG_STRING, this, description(message->messageName()).characters());
        }

        // Re-schedule ourselves *before* we dispatch the messages because we want to process follow-up messages if the client
        // spins a nested run loop while we're dispatching a message. Note that this means we can re-enter this method.
        if (!m_incomingMessages.isEmpty()) {
            dispatcher().dispatch([protectedThis = Ref { *this }] {
                protectedThis->dispatchIncomingMessages();
            });
        }
    }

    dispatchMessage(makeUniqueRefFromNonNullUniquePtr(WTFMove(message)));

    for (size_t i = 1; i < messagesToProcess; ++i) {
        {
            Locker locker { m_incomingMessagesLock };
            if (m_incomingMessages.isEmpty())
                return;

            message = m_incomingMessages.takeFirst().moveToUniquePtr();
        }
        dispatchMessage(makeUniqueRefFromNonNullUniquePtr(WTFMove(message)));
    }
}

void Connection::addAsyncReplyHandler(AsyncReplyHandler&& handler)
{
    Locker locker { m_incomingMessagesLock };
    auto result = m_asyncReplyHandlers.add(*handler.replyID, WTFMove(handler.completionHandler));
    ASSERT_UNUSED(result, result.isNewEntry);
}

void Connection::addAsyncReplyHandlerWithDispatcher(AsyncReplyHandlerWithDispatcher&& handler)
{
    Locker locker { m_incomingMessagesLock };
    auto result = m_asyncReplyHandlerWithDispatchers.add(*handler.replyID, WTFMove(handler.completionHandler));
    ASSERT_UNUSED(result, result.isNewEntry);
}

void Connection::cancelAsyncReplyHandlers()
{
    AsyncReplyHandlerMap map;
    AsyncReplyHandlerWithDispatcherMap mapDispatcher;
    {
        Locker locker { m_incomingMessagesLock };
        map.swap(m_asyncReplyHandlers);
        mapDispatcher.swap(m_asyncReplyHandlerWithDispatchers);
    }

    for (auto& handler : map.values()) {
        if (handler)
            handler(nullptr, nullptr);
    }

    for (auto& handlerWithDispatcher : mapDispatcher.values()) {
        if (handlerWithDispatcher)
            handlerWithDispatcher(nullptr, nullptr);
    }
}

CompletionHandler<void(Connection*, Decoder*)> Connection::takeAsyncReplyHandler(AsyncReplyID replyID)
{
    Locker locker { m_incomingMessagesLock };
    if (!m_asyncReplyHandlers.isValidKey(replyID))
        return nullptr;
    return m_asyncReplyHandlers.take(replyID);
}

bool Connection::isAsyncReplyHandlerWithDispatcher(AsyncReplyID replyID)
{
    Locker locker { m_incomingMessagesLock };
    return m_asyncReplyHandlerWithDispatchers.isValidKey(replyID) && m_asyncReplyHandlerWithDispatchers.contains(replyID);
}

CompletionHandler<void(Connection*, std::unique_ptr<Decoder>&&)> Connection::takeAsyncReplyHandlerWithDispatcher(AsyncReplyID replyID)
{
    Locker locker { m_incomingMessagesLock };
    return takeAsyncReplyHandlerWithDispatcherWithLockHeld(replyID);
}

CompletionHandler<void(Connection*, std::unique_ptr<Decoder>&&)> Connection::takeAsyncReplyHandlerWithDispatcherWithLockHeld(AsyncReplyID replyID)
{
    assertIsHeld(m_incomingMessagesLock);
    if (!m_asyncReplyHandlerWithDispatchers.isValidKey(replyID))
        return { };
    return m_asyncReplyHandlerWithDispatchers.take(replyID);
}

void Connection::wakeUpRunLoop()
{
    if (!isValid())
        return;
    if (&dispatcher() == &RunLoop::mainSingleton())
        RunLoop::mainSingleton().wakeUp();
}

template<typename F>
void Connection::dispatchToClient(F&& clientRunLoopTask)
{
    Locker lock { m_incomingMessagesLock };
    dispatchToClientWithIncomingMessagesLock(std::forward<F>(clientRunLoopTask));
}

template<typename F>
void Connection::dispatchToClientWithIncomingMessagesLock(F&& clientRunLoopTask)
{
    if (!m_syncState)
        return;
    dispatcher().dispatch(WTFMove(clientRunLoopTask));
}

#if !USE(UNIX_DOMAIN_SOCKETS) && !OS(DARWIN) && !OS(WINDOWS)
std::optional<Connection::ConnectionIdentifierPair> Connection::createConnectionIdentifierPair()
{
    notImplemented();
    return std::nullopt;
}
#endif

ASCIILiteral errorAsString(Error error)
{
    switch (error) {
    case Error::NoError: return "NoError"_s;
    case Error::InvalidConnection: return "InvalidConnection"_s;
    case Error::NoConnectionForIdentifier: return "NoConnectionForIdentifier"_s;
    case Error::NoMessageSenderConnection: return "NoMessageSenderConnection"_s;
    case Error::Timeout: return "Timeout"_s;
    case Error::Unspecified: return "Unspecified"_s;
    case Error::MultipleWaitingClients: return "MultipleWaitingClients"_s;
    case Error::AttemptingToWaitOnClosedConnection: return "AttemptingToWaitOnClosedConnection"_s;
    case Error::WaitingOnAlreadyDispatchedMessage: return "WaitingOnAlreadyDispatchedMessage"_s;
    case Error::AttemptingToWaitInsideSyncMessageHandling: return "AttemptingToWaitInsideSyncMessageHandling"_s;
    case Error::SyncMessageInterruptedWait: return "SyncMessageInterruptedWait"_s;
    case Error::SyncMessageCancelled: return "SyncMessageCancelled"_s;
    case Error::CantWaitForSyncReplies: return "CantWaitForSyncReplies"_s;
    case Error::FailedToEncodeMessageArguments: return "FailedToEncodeMessageArguments"_s;
    case Error::FailedToDecodeReplyArguments: return "FailedToDecodeReplyArguments"_s;
    case Error::FailedToFindReplyHandler: return "FailedToFindReplyHandler"_s;
    case Error::FailedToAcquireBufferSpan: return "FailedToAcquireBufferSpan"_s;
    case Error::FailedToAcquireReplyBufferSpan: return "FailedToAcquireReplyBufferSpan"_s;
    case Error::StreamConnectionEncodingError: return "StreamConnectionEncodingError"_s;
    }

    return ""_s;
}

static bool s_shouldCrashOnMessageCheckFailure { false };

bool Connection::shouldCrashOnMessageCheckFailure()
{
    return s_shouldCrashOnMessageCheckFailure;
}

void Connection::setShouldCrashOnMessageCheckFailure(bool shouldCrash)
{
    s_shouldCrashOnMessageCheckFailure = shouldCrash;
}

} // namespace IPC
