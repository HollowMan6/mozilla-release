/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SmsParent.h"
#include "nsISmsService.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "Constants.h"
#include "nsIDOMSmsMessage.h"
#include "mozilla/unused.h"
#include "SmsMessage.h"
#include "nsISmsDatabaseService.h"
#include "SmsFilter.h"

namespace mozilla {
namespace dom {
namespace sms {

nsTArray<SmsParent*>* SmsParent::gSmsParents = nullptr;

NS_IMPL_ISUPPORTS1(SmsParent, nsIObserver)

/* static */ void
SmsParent::GetAll(nsTArray<SmsParent*>& aArray)
{
  if (!gSmsParents) {
    aArray.Clear();
    return;
  }

  aArray = *gSmsParents;
}

SmsParent::SmsParent()
{
  if (!gSmsParents) {
    gSmsParents = new nsTArray<SmsParent*>();
  }

  gSmsParents->AppendElement(this);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return;
  }

  obs->AddObserver(this, kSmsReceivedObserverTopic, false);
  obs->AddObserver(this, kSmsSentObserverTopic, false);
  obs->AddObserver(this, kSmsDeliverySuccessObserverTopic, false);
  obs->AddObserver(this, kSmsDeliveryErrorObserverTopic, false);
}

void
SmsParent::ActorDestroy(ActorDestroyReason why)
{
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return;
  }

  obs->RemoveObserver(this, kSmsReceivedObserverTopic);
  obs->RemoveObserver(this, kSmsSentObserverTopic);
  obs->RemoveObserver(this, kSmsDeliverySuccessObserverTopic);
  obs->RemoveObserver(this, kSmsDeliveryErrorObserverTopic);

  NS_ASSERTION(gSmsParents, "gSmsParents can't be null at that point!");
  gSmsParents->RemoveElement(this);
  if (gSmsParents->Length() == 0) {
    delete gSmsParents;
    gSmsParents = nullptr;
  }
}

NS_IMETHODIMP
SmsParent::Observe(nsISupports* aSubject, const char* aTopic,
                   const PRUnichar* aData)
{
  if (!strcmp(aTopic, kSmsReceivedObserverTopic)) {
    nsCOMPtr<nsIDOMMozSmsMessage> message = do_QueryInterface(aSubject);
    if (!message) {
      NS_ERROR("Got a 'sms-received' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifyReceivedMessage(static_cast<SmsMessage*>(message.get())->GetData());
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsSentObserverTopic)) {
    nsCOMPtr<nsIDOMMozSmsMessage> message = do_QueryInterface(aSubject);
    if (!message) {
      NS_ERROR("Got a 'sms-sent' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifySentMessage(static_cast<SmsMessage*>(message.get())->GetData());
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsDeliverySuccessObserverTopic)) {
    nsCOMPtr<nsIDOMMozSmsMessage> message = do_QueryInterface(aSubject);
    if (!message) {
      NS_ERROR("Got a 'sms-delivery-success' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifyDeliverySuccessMessage(static_cast<SmsMessage*>(message.get())->GetData());
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsDeliveryErrorObserverTopic)) {
    nsCOMPtr<nsIDOMMozSmsMessage> message = do_QueryInterface(aSubject);
    if (!message) {
      NS_ERROR("Got a 'sms-delivery-error' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifyDeliveryErrorMessage(static_cast<SmsMessage*>(message.get())->GetData());
    return NS_OK;
  }

  return NS_OK;
}

bool
SmsParent::RecvHasSupport(bool* aHasSupport)
{
  *aHasSupport = false;

  nsCOMPtr<nsISmsService> smsService = do_GetService(SMS_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsService, true);

  smsService->HasSupport(aHasSupport);
  return true;
}

bool
SmsParent::RecvGetNumberOfMessagesForText(const nsString& aText, uint16_t* aResult)
{
  *aResult = 0;

  nsCOMPtr<nsISmsService> smsService = do_GetService(SMS_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsService, true);

  smsService->GetNumberOfMessagesForText(aText, aResult);
  return true;
}

bool
SmsParent::RecvSendMessage(const nsString& aNumber, const nsString& aMessage,
                           const int32_t& aRequestId, const uint64_t& aProcessId)
{
  nsCOMPtr<nsISmsService> smsService = do_GetService(SMS_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsService, true);

  smsService->Send(aNumber, aMessage, aRequestId, aProcessId);
  return true;
}

bool
SmsParent::RecvSaveReceivedMessage(const nsString& aSender,
                                   const nsString& aBody,
                                   const nsString& aMessageClass,
                                   const uint64_t& aDate,
                                   int32_t* aId)
{
  *aId = -1;

  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->SaveReceivedMessage(aSender, aBody, aMessageClass, aDate, aId);
  return true;
}

bool
SmsParent::RecvSaveSentMessage(const nsString& aRecipient,
                               const nsString& aBody,
                               const uint64_t& aDate, int32_t* aId)
{
  *aId = -1;

  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->SaveSentMessage(aRecipient, aBody, aDate, aId);
  return true;
}

bool
SmsParent::RecvSetMessageDeliveryStatus(const int32_t& aMessageId,
                                        const nsString& aDeliveryStatus)
{
  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->SetMessageDeliveryStatus(aMessageId, aDeliveryStatus);
  return true;
}

bool
SmsParent::RecvGetMessage(const int32_t& aMessageId, const int32_t& aRequestId,
                          const uint64_t& aProcessId)
{
  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->GetMessageMoz(aMessageId, aRequestId, aProcessId);
  return true;
}

bool
SmsParent::RecvDeleteMessage(const int32_t& aMessageId, const int32_t& aRequestId,
                             const uint64_t& aProcessId)
{
  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->DeleteMessage(aMessageId, aRequestId, aProcessId);
  return true;
}

bool
SmsParent::RecvCreateMessageList(const SmsFilterData& aFilter,
                                 const bool& aReverse,
                                 const int32_t& aRequestId,
                                 const uint64_t& aProcessId)
{
  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  nsCOMPtr<nsIDOMMozSmsFilter> filter = new SmsFilter(aFilter);
  smsDBService->CreateMessageList(filter, aReverse, aRequestId, aProcessId);

  return true;
}

bool
SmsParent::RecvGetNextMessageInList(const int32_t& aListId,
                                    const int32_t& aRequestId,
                                    const uint64_t& aProcessId)
{
  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->GetNextMessageInList(aListId, aRequestId, aProcessId);

  return true;
}

bool
SmsParent::RecvClearMessageList(const int32_t& aListId)
{
  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->ClearMessageList(aListId);

  return true;
}

bool
SmsParent::RecvMarkMessageRead(const int32_t& aMessageId,
                               const bool& aValue,
                               const int32_t& aRequestId,
                               const uint64_t& aProcessId)
{
  nsCOMPtr<nsISmsDatabaseService> smsDBService =
    do_GetService(SMS_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsDBService, true);

  smsDBService->MarkMessageRead(aMessageId, aValue, aRequestId, aProcessId);
  return true;
}

} // namespace sms
} // namespace dom
} // namespace mozilla
