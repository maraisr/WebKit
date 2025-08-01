/*
 * Copyright (C) 2015-2020 Apple Inc. All rights reserved.
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

#pragma once

#if ENABLE(APPLE_PAY)

#include "MessageReceiver.h"
#include "MessageSender.h"
#include "PaymentAuthorizationPresenter.h"
#include "SharedPreferencesForWebProcess.h"
#include "WebPageProxyIdentifier.h"
#include <WebCore/PageIdentifier.h>
#include <WebCore/PaymentHeaders.h>
#include <wtf/Forward.h>
#include <wtf/RetainPtr.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakObjCPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/WorkQueue.h>

#if PLATFORM(COCOA)
#include "CocoaWindow.h"
#endif

OBJC_CLASS PKPaymentSetupViewController;
OBJC_CLASS UIViewController;

namespace IPC {
class Connection;
enum class ReceiverName : uint8_t;
}

namespace WebCore {
class Payment;
class PaymentContact;
class PaymentMerchantSession;
class PaymentMethod;
struct ApplePayCouponCodeUpdate;
struct ApplePayPaymentMethodUpdate;
struct ApplePayShippingContactUpdate;
struct ApplePayShippingMethod;
struct ApplePayShippingMethodUpdate;
}

OBJC_CLASS NSObject;
OBJC_CLASS PKPaymentAuthorizationViewController;
OBJC_CLASS PKPaymentRequest;
OBJC_CLASS UIViewController;

#if PLATFORM(IOS) || PLATFORM(VISION)
OBJC_CLASS PKPaymentSetupViewController;
#endif

namespace WebKit {

class PaymentSetupConfiguration;
class PaymentSetupFeatures;

class WebPaymentCoordinatorProxy final
    : public IPC::MessageReceiver
    , private IPC::MessageSender
    , public PaymentAuthorizationPresenter::Client
    , public RefCounted<WebPaymentCoordinatorProxy> {
    WTF_MAKE_TZONE_ALLOCATED(WebPaymentCoordinatorProxy);
public:
    USING_CAN_MAKE_WEAKPTR(MessageReceiver);

    struct Client : public CanMakeWeakPtr<Client>, public CanMakeCheckedPtr<Client> {
        WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(Client);
        WTF_STRUCT_OVERRIDE_DELETE_FOR_CHECKED_PTR(Client);

        virtual ~Client() = default;

        virtual IPC::Connection* paymentCoordinatorConnection(const WebPaymentCoordinatorProxy&) = 0;
        virtual const String& paymentCoordinatorBoundInterfaceIdentifier(const WebPaymentCoordinatorProxy&) = 0;
        virtual const String& paymentCoordinatorSourceApplicationBundleIdentifier(const WebPaymentCoordinatorProxy&) = 0;
        virtual const String& paymentCoordinatorSourceApplicationSecondaryIdentifier(const WebPaymentCoordinatorProxy&) = 0;
        virtual void paymentCoordinatorAddMessageReceiver(WebPaymentCoordinatorProxy&, IPC::ReceiverName, IPC::MessageReceiver&) = 0;
        virtual void paymentCoordinatorRemoveMessageReceiver(WebPaymentCoordinatorProxy&, IPC::ReceiverName) = 0;
#if PLATFORM(IOS_FAMILY)
        virtual UIViewController *paymentCoordinatorPresentingViewController(const WebPaymentCoordinatorProxy&) = 0;
#if ENABLE(APPLE_PAY_REMOTE_UI_USES_SCENE)
        virtual void getWindowSceneAndBundleIdentifierForPaymentPresentation(WebPageProxyIdentifier, CompletionHandler<void(const String&, const String&)>&&) = 0;
#endif
        virtual const String& paymentCoordinatorCTDataConnectionServiceType(const WebPaymentCoordinatorProxy&) = 0;
        virtual Ref<PaymentAuthorizationPresenter> paymentCoordinatorAuthorizationPresenter(WebPaymentCoordinatorProxy&, PKPaymentRequest *) = 0;
#endif
        virtual CocoaWindow *paymentCoordinatorPresentingWindow(const WebPaymentCoordinatorProxy&) const = 0;
        virtual void getPaymentCoordinatorEmbeddingUserAgent(WebPageProxyIdentifier, CompletionHandler<void(const String&)>&&) = 0;
        virtual std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebPaymentMessages() const = 0;
    };

    static Ref<WebPaymentCoordinatorProxy> create(Client&);

    friend class NetworkConnectionToWebProcess;
    ~WebPaymentCoordinatorProxy();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    void webProcessExited();
    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess() const
    {
        CheckedPtr client = m_client.get();
        return client ? client->sharedPreferencesForWebPaymentMessages() : std::nullopt;
    }

private:
    explicit WebPaymentCoordinatorProxy(Client&);
    Ref<WorkQueue> protectedCanMakePaymentsQueue() const;
    CheckedPtr<Client> checkedClient() const { return m_client.get(); }

    // IPC::MessageReceiver
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) override;
    bool didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, UniqueRef<IPC::Encoder>&) override;

    // IPC::MessageSender
    IPC::Connection* messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final;
    
    // PaymentAuthorizationPresenter::Client
    void presenterDidAuthorizePayment(PaymentAuthorizationPresenter&, const WebCore::Payment&) final;
    void presenterDidFinish(PaymentAuthorizationPresenter&, WebCore::PaymentSessionError&&) final;
    void presenterDidSelectPaymentMethod(PaymentAuthorizationPresenter&, const WebCore::PaymentMethod&) final;
    void presenterDidSelectShippingContact(PaymentAuthorizationPresenter&, const WebCore::PaymentContact&) final;
    void presenterDidSelectShippingMethod(PaymentAuthorizationPresenter&, const WebCore::ApplePayShippingMethod&) final;
#if ENABLE(APPLE_PAY_COUPON_CODE)
    void presenterDidChangeCouponCode(PaymentAuthorizationPresenter&, const String& couponCode) final;
#endif
    void presenterWillValidateMerchant(PaymentAuthorizationPresenter&, const URL&) final;
    CocoaWindow *presentingWindowForPaymentAuthorization(PaymentAuthorizationPresenter&) const final;

    // Message handlers
    void canMakePayments(CompletionHandler<void(bool)>&&);
    void canMakePaymentsWithActiveCard(const String& merchantIdentifier, const String& domainName, CompletionHandler<void(bool)>&&);
    void openPaymentSetup(const String& merchantIdentifier, const String& domainName, CompletionHandler<void(bool)>&&);
    void showPaymentUI(WebCore::PageIdentifier destinationID, WebPageProxyIdentifier, const URL& originatingURL, const Vector<URL>& linkIconURLs, const WebCore::ApplePaySessionPaymentRequest&, CompletionHandler<void(bool)>&&);
    void completeMerchantValidation(const WebCore::PaymentMerchantSession&);
    void completeShippingMethodSelection(std::optional<WebCore::ApplePayShippingMethodUpdate>&&);
    void completeShippingContactSelection(std::optional<WebCore::ApplePayShippingContactUpdate>&&);
    void completePaymentMethodSelection(std::optional<WebCore::ApplePayPaymentMethodUpdate>&&);
#if ENABLE(APPLE_PAY_COUPON_CODE)
    void completeCouponCodeChange(std::optional<WebCore::ApplePayCouponCodeUpdate>&&);
#endif
    void completePaymentSession(WebCore::ApplePayPaymentAuthorizationResult&&);
    void abortPaymentSession();
    void cancelPaymentSession();

    void getSetupFeatures(const PaymentSetupConfiguration&, CompletionHandler<void(PaymentSetupFeatures&&)>&&);
    void beginApplePaySetup(const PaymentSetupConfiguration&, const PaymentSetupFeatures&, CompletionHandler<void(bool)>&&);
    void endApplePaySetup();
    void platformBeginApplePaySetup(const PaymentSetupConfiguration&, const PaymentSetupFeatures&, CompletionHandler<void(bool)>&&);
    void platformEndApplePaySetup();

    bool canBegin() const;
    bool canCancel() const;
    bool canCompletePayment() const;
    bool canAbort() const;

    void didReachFinalState(WebCore::PaymentSessionError&& = { });

    void platformCanMakePayments(CompletionHandler<void(bool)>&&);
    void platformCanMakePaymentsWithActiveCard(const String& merchantIdentifier, const String& domainName, WTF::Function<void(bool)>&& completionHandler);
    void platformOpenPaymentSetup(const String& merchantIdentifier, const String& domainName, WTF::Function<void(bool)>&& completionHandler);
    void platformShowPaymentUI(WebPageProxyIdentifier, const URL& originatingURL, const Vector<URL>& linkIconURLs, const WebCore::ApplePaySessionPaymentRequest&, CompletionHandler<void(bool)>&&);
    void platformCompleteMerchantValidation(const WebCore::PaymentMerchantSession&);
    void platformCompleteShippingMethodSelection(std::optional<WebCore::ApplePayShippingMethodUpdate>&&);
    void platformCompleteShippingContactSelection(std::optional<WebCore::ApplePayShippingContactUpdate>&&);
    void platformCompletePaymentMethodSelection(std::optional<WebCore::ApplePayPaymentMethodUpdate>&&);
#if ENABLE(APPLE_PAY_COUPON_CODE)
    void platformCompleteCouponCodeChange(std::optional<WebCore::ApplePayCouponCodeUpdate>&&);
#endif
    void platformCompletePaymentSession(WebCore::ApplePayPaymentAuthorizationResult&&);
    void platformHidePaymentUI();
#if PLATFORM(COCOA)
    RetainPtr<PKPaymentRequest> platformPaymentRequest(const URL& originatingURL, const Vector<URL>& linkIconURLs, const WebCore::ApplePaySessionPaymentRequest&);
    void platformSetPaymentRequestUserAgent(PKPaymentRequest *, const String& userAgent);
#endif

    RefPtr<PaymentAuthorizationPresenter> protectedAuthorizationPresenter() { return m_authorizationPresenter; }

    WeakPtr<Client> m_client;
    std::optional<WebCore::PageIdentifier> m_destinationID;

    enum class State : uint16_t {
        // Idle - Nothing's happening.
        Idle,

        // Activating - Waiting to show the payment UI.
        Activating,

        // Active - Showing payment UI.
        Active,

        // Authorized - Dispatching the authorized event and waiting for the paymentSessionCompleted message.
        Authorized,

        // ShippingMethodSelected - Dispatching the shippingmethodselected event and waiting for a reply.
        ShippingMethodSelected,

        // ShippingContactSelected - Dispatching the shippingcontactselected event and waiting for a reply.
        ShippingContactSelected,

        // PaymentMethodSelected - Dispatching the paymentmethodselected event and waiting for a reply.
        PaymentMethodSelected,

#if ENABLE(APPLE_PAY_COUPON_CODE)
        // CouponCodeChanged - Dispatching the couponcodechanged event and waiting for a reply.
        CouponCodeChanged,
#endif

        // Deactivating - Could not complete the payment and is about to idle.
        // Currently only transitions here when the web process terminates while the payment coordinator is active.
        Deactivating,

        // Completing - Completing the payment and waiting for presenterDidFinish to be called.
        Completing,
    } m_state { State::Idle };

    enum class MerchantValidationState {
        // Idle - Nothing's happening.
        Idle,

        // Validating - Dispatching the validatemerchant event and waiting for a reply.
        Validating,

        // ValidationComplete - A merchant session has been sent along to PassKit.
        ValidationComplete
    } m_merchantValidationState { MerchantValidationState::Idle };

    RefPtr<PaymentAuthorizationPresenter> m_authorizationPresenter;
    Ref<WorkQueue> m_canMakePaymentsQueue;

#if PLATFORM(MAC)
    uint64_t m_showPaymentUIRequestSeed { 0 };
    RetainPtr<NSWindow> m_sheetWindow;
    RetainPtr<NSObject> m_sheetWindowWillCloseObserver;
#endif
#if PLATFORM(IOS) || PLATFORM(VISION)
    WeakObjCPtr<PKPaymentSetupViewController> m_paymentSetupViewController;
#endif
};

}

#endif
