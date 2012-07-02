/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "nsIOService.h"
#include "nsHttpHandler.h"
#include "nsHttpTransaction.h"
#include "nsHttpConnection.h"
#include "nsHttpRequestHead.h"
#include "nsHttpResponseHead.h"
#include "nsHttpChunkedDecoder.h"
#include "nsTransportUtils.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsIOService.h"
#include "nsAtomicRefcnt.h"

#include "nsISeekableStream.h"
#include "nsISocketTransport.h"
#include "nsMultiplexInputStream.h"
#include "nsStringStream.h"

#include "nsComponentManagerUtils.h" // do_CreateInstance
#include "nsServiceManagerUtils.h"   // do_GetService
#include "nsIHttpActivityObserver.h"

#include "mozilla/FunctionTimer.h"

using namespace mozilla;

//-----------------------------------------------------------------------------

#ifdef DEBUG
// defined by the socket transport service while active
extern PRThread *gSocketThread;
#endif

//-----------------------------------------------------------------------------

static NS_DEFINE_CID(kMultiplexInputStream, NS_MULTIPLEXINPUTSTREAM_CID);

// Place a limit on how much non-compliant HTTP can be skipped while
// looking for a response header
#define MAX_INVALID_RESPONSE_BODY_SIZE (1024 * 128)

//-----------------------------------------------------------------------------
// helpers
//-----------------------------------------------------------------------------

#if defined(PR_LOGGING)
static void
LogHeaders(const char *lines)
{
    nsCAutoString buf;
    char *p;
    while ((p = PL_strstr(lines, "\r\n")) != nsnull) {
        buf.Assign(lines, p - lines);
        if (PL_strcasestr(buf.get(), "authorization: ") != nsnull) {
            char *p = PL_strchr(PL_strchr(buf.get(), ' ')+1, ' ');
            while (*++p) *p = '*';
        }
        LOG3(("  %s\n", buf.get()));
        lines = p + 2;
    }
}
#endif

//-----------------------------------------------------------------------------
// nsHttpTransaction <public>
//-----------------------------------------------------------------------------

nsHttpTransaction::nsHttpTransaction()
    : mRequestSize(0)
    , mConnection(nsnull)
    , mConnInfo(nsnull)
    , mRequestHead(nsnull)
    , mResponseHead(nsnull)
    , mContentLength(-1)
    , mContentRead(0)
    , mInvalidResponseBytesRead(0)
    , mChunkedDecoder(nsnull)
    , mStatus(NS_OK)
    , mPriority(0)
    , mRestartCount(0)
    , mCaps(0)
    , mClassification(CLASS_GENERAL)
    , mPipelinePosition(0)
    , mClosed(false)
    , mConnected(false)
    , mHaveStatusLine(false)
    , mHaveAllHeaders(false)
    , mTransactionDone(false)
    , mResponseIsComplete(false)
    , mDidContentStart(false)
    , mNoContent(false)
    , mSentData(false)
    , mReceivedData(false)
    , mStatusEventPending(false)
    , mHasRequestBody(false)
    , mSSLConnectFailed(false)
    , mHttpResponseMatched(false)
    , mPreserveStream(false)
    , mReportedStart(false)
    , mReportedResponseHeader(false)
    , mForTakeResponseHead(nsnull)
    , mResponseHeadTaken(false)
{
    LOG(("Creating nsHttpTransaction @%x\n", this));
    gHttpHandler->GetMaxPipelineObjectSize(&mMaxPipelineObjectSize);
}

nsHttpTransaction::~nsHttpTransaction()
{
    LOG(("Destroying nsHttpTransaction @%x\n", this));

    NS_IF_RELEASE(mConnection);
    NS_IF_RELEASE(mConnInfo);

    delete mResponseHead;
    delete mForTakeResponseHead;
    delete mChunkedDecoder;
}

nsHttpTransaction::Classifier
nsHttpTransaction::Classify()
{
    if (!(mCaps & NS_HTTP_ALLOW_PIPELINING))
        return (mClassification = CLASS_SOLO);

    if (mRequestHead->PeekHeader(nsHttp::If_Modified_Since) ||
        mRequestHead->PeekHeader(nsHttp::If_None_Match))
        return (mClassification = CLASS_REVALIDATION);

    const char *accept = mRequestHead->PeekHeader(nsHttp::Accept);
    if (accept && !PL_strncmp(accept, "image/", 6))
        return (mClassification = CLASS_IMAGE);

    if (accept && !PL_strncmp(accept, "text/css", 8))
        return (mClassification = CLASS_SCRIPT);

    mClassification = CLASS_GENERAL;

    PRInt32 queryPos = mRequestHead->RequestURI().FindChar('?');
    if (queryPos == kNotFound) {
        if (StringEndsWith(mRequestHead->RequestURI(),
                           NS_LITERAL_CSTRING(".js")))
            mClassification = CLASS_SCRIPT;
    }
    else if (queryPos >= 3 &&
             Substring(mRequestHead->RequestURI(), queryPos - 3, 3).
             EqualsLiteral(".js")) {
        mClassification = CLASS_SCRIPT;
    }

    return mClassification;
}

nsresult
nsHttpTransaction::Init(PRUint8 caps,
                        nsHttpConnectionInfo *cinfo,
                        nsHttpRequestHead *requestHead,
                        nsIInputStream *requestBody,
                        bool requestBodyHasHeaders,
                        nsIEventTarget *target,
                        nsIInterfaceRequestor *callbacks,
                        nsITransportEventSink *eventsink,
                        nsIAsyncInputStream **responseBody)
{
    NS_TIME_FUNCTION;

    nsresult rv;

    LOG(("nsHttpTransaction::Init [this=%x caps=%x]\n", this, caps));

    NS_ASSERTION(cinfo, "ouch");
    NS_ASSERTION(requestHead, "ouch");
    NS_ASSERTION(target, "ouch");

    mActivityDistributor = do_GetService(NS_HTTPACTIVITYDISTRIBUTOR_CONTRACTID, &rv);
    if (NS_FAILED(rv)) return rv;

    bool activityDistributorActive;
    rv = mActivityDistributor->GetIsActive(&activityDistributorActive);
    if (NS_SUCCEEDED(rv) && activityDistributorActive) {
        // there are some observers registered at activity distributor, gather
        // nsISupports for the channel that called Init()
        mChannel = do_QueryInterface(eventsink);
        LOG(("nsHttpTransaction::Init() " \
             "mActivityDistributor is active " \
             "this=%x", this));
    } else {
        // there is no observer, so don't use it
        activityDistributorActive = false;
        mActivityDistributor = nsnull;
    }

    // create transport event sink proxy. it coalesces all events if and only 
    // if the activity observer is not active. when the observer is active
    // we need not to coalesce any events to get all expected notifications
    // of the transaction state, necessary for correct debugging and logging.
    rv = net_NewTransportEventSinkProxy(getter_AddRefs(mTransportSink),
                                        eventsink, target,
                                        !activityDistributorActive);
    if (NS_FAILED(rv)) return rv;

    NS_ADDREF(mConnInfo = cinfo);
    mCallbacks = callbacks;
    mConsumerTarget = target;
    mCaps = caps;

    if (requestHead->Method() == nsHttp::Head)
        mNoContent = true;

    // Make sure that there is "Content-Length: 0" header in the requestHead
    // in case of POST and PUT methods when there is no requestBody and
    // requestHead doesn't contain "Transfer-Encoding" header.
    //
    // RFC1945 section 7.2.2:
    //   HTTP/1.0 requests containing an entity body must include a valid
    //   Content-Length header field.
    //
    // RFC2616 section 4.4:
    //   For compatibility with HTTP/1.0 applications, HTTP/1.1 requests
    //   containing a message-body MUST include a valid Content-Length header
    //   field unless the server is known to be HTTP/1.1 compliant.
    if ((requestHead->Method() == nsHttp::Post || requestHead->Method() == nsHttp::Put) &&
        !requestBody && !requestHead->PeekHeader(nsHttp::Transfer_Encoding)) {
        requestHead->SetHeader(nsHttp::Content_Length, NS_LITERAL_CSTRING("0"));
    }

    // grab a weak reference to the request head
    mRequestHead = requestHead;

    // make sure we eliminate any proxy specific headers from 
    // the request if we are talking HTTPS via a SSL tunnel.
    bool pruneProxyHeaders = 
        cinfo->ShouldForceConnectMethod() ||
        (cinfo->UsingSSL() && cinfo->UsingHttpProxy());
    
    mReqHeaderBuf.Truncate();
    requestHead->Flatten(mReqHeaderBuf, pruneProxyHeaders);

#if defined(PR_LOGGING)
    if (LOG3_ENABLED()) {
        LOG3(("http request [\n"));
        LogHeaders(mReqHeaderBuf.get());
        LOG3(("]\n"));
    }
#endif

    // If the request body does not include headers or if there is no request
    // body, then we must add the header/body separator manually.
    if (!requestBodyHasHeaders || !requestBody)
        mReqHeaderBuf.AppendLiteral("\r\n");

    // report the request header
    if (mActivityDistributor)
        mActivityDistributor->ObserveActivity(
            mChannel,
            NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
            NS_HTTP_ACTIVITY_SUBTYPE_REQUEST_HEADER,
            PR_Now(), LL_ZERO,
            mReqHeaderBuf);

    // Create a string stream for the request header buf (the stream holds
    // a non-owning reference to the request header data, so we MUST keep
    // mReqHeaderBuf around).
    nsCOMPtr<nsIInputStream> headers;
    rv = NS_NewByteInputStream(getter_AddRefs(headers),
                               mReqHeaderBuf.get(),
                               mReqHeaderBuf.Length());
    if (NS_FAILED(rv)) return rv;

    if (requestBody) {
        mHasRequestBody = true;

        // wrap the headers and request body in a multiplexed input stream.
        nsCOMPtr<nsIMultiplexInputStream> multi =
            do_CreateInstance(kMultiplexInputStream, &rv);
        if (NS_FAILED(rv)) return rv;

        rv = multi->AppendStream(headers);
        if (NS_FAILED(rv)) return rv;

        rv = multi->AppendStream(requestBody);
        if (NS_FAILED(rv)) return rv;

        // wrap the multiplexed input stream with a buffered input stream, so
        // that we write data in the largest chunks possible.  this is actually
        // necessary to workaround some common server bugs (see bug 137155).
        rv = NS_NewBufferedInputStream(getter_AddRefs(mRequestStream), multi,
                                       nsIOService::gDefaultSegmentSize);
        if (NS_FAILED(rv)) return rv;
    }
    else
        mRequestStream = headers;

    rv = mRequestStream->Available(&mRequestSize);
    if (NS_FAILED(rv)) return rv;

    // create pipe for response stream
    rv = NS_NewPipe2(getter_AddRefs(mPipeIn),
                     getter_AddRefs(mPipeOut),
                     true, true,
                     nsIOService::gDefaultSegmentSize,
                     nsIOService::gDefaultSegmentCount);
    if (NS_FAILED(rv)) return rv;

    Classify();

    NS_ADDREF(*responseBody = mPipeIn);
    return NS_OK;
}

nsAHttpConnection *
nsHttpTransaction::Connection()
{
    return mConnection;
}

nsHttpResponseHead *
nsHttpTransaction::TakeResponseHead()
{
    NS_ABORT_IF_FALSE(!mResponseHeadTaken, "TakeResponseHead called 2x");

    // Lock RestartInProgress() and TakeResponseHead() against main thread
    MutexAutoLock lock(*nsHttp::GetLock());

    mResponseHeadTaken = true;

    // Prefer mForTakeResponseHead over mResponseHead. It is always a complete
    // set of headers.
    nsHttpResponseHead *head;
    if (mForTakeResponseHead) {
        head = mForTakeResponseHead;
        mForTakeResponseHead = nsnull;
        return head;
    }
    
    // Even in OnStartRequest() the headers won't be available if we were
    // canceled
    if (!mHaveAllHeaders) {
        NS_WARNING("response headers not available or incomplete");
        return nsnull;
    }

    head = mResponseHead;
    mResponseHead = nsnull;
    return head;
}

void
nsHttpTransaction::SetSSLConnectFailed()
{
    mSSLConnectFailed = true;
}

nsHttpRequestHead *
nsHttpTransaction::RequestHead()
{
    return mRequestHead;
}

PRUint32
nsHttpTransaction::Http1xTransactionCount()
{
  return 1;
}

nsresult
nsHttpTransaction::TakeSubTransactions(
    nsTArray<nsRefPtr<nsAHttpTransaction> > &outTransactions)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

//----------------------------------------------------------------------------
// nsHttpTransaction::nsAHttpTransaction
//----------------------------------------------------------------------------

void
nsHttpTransaction::SetConnection(nsAHttpConnection *conn)
{
    NS_IF_RELEASE(mConnection);
    NS_IF_ADDREF(mConnection = conn);
}

void
nsHttpTransaction::GetSecurityCallbacks(nsIInterfaceRequestor **cb,
                                        nsIEventTarget        **target)
{
    NS_IF_ADDREF(*cb = mCallbacks);
    if (target)
        NS_IF_ADDREF(*target = mConsumerTarget);
}

void
nsHttpTransaction::OnTransportStatus(nsITransport* transport,
                                     nsresult status, PRUint64 progress)
{
    LOG(("nsHttpTransaction::OnSocketStatus [this=%x status=%x progress=%llu]\n",
        this, status, progress));

    if (TimingEnabled()) {
        if (status == nsISocketTransport::STATUS_RESOLVING) {
            mTimings.domainLookupStart = mozilla::TimeStamp::Now();
        } else if (status == nsISocketTransport::STATUS_RESOLVED) {
            mTimings.domainLookupEnd = mozilla::TimeStamp::Now();
        } else if (status == nsISocketTransport::STATUS_CONNECTING_TO) {
            mTimings.connectStart = mozilla::TimeStamp::Now();
        } else if (status == nsISocketTransport::STATUS_CONNECTED_TO) {
            mTimings.connectEnd = mozilla::TimeStamp::Now();
        }
    }

    if (!mTransportSink)
        return;

    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    // Need to do this before the STATUS_RECEIVING_FROM check below, to make
    // sure that the activity distributor gets told about all status events.
    if (mActivityDistributor) {
        // upon STATUS_WAITING_FOR; report request body sent
        if ((mHasRequestBody) &&
            (status == nsISocketTransport::STATUS_WAITING_FOR))
            mActivityDistributor->ObserveActivity(
                mChannel,
                NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
                NS_HTTP_ACTIVITY_SUBTYPE_REQUEST_BODY_SENT,
                PR_Now(), LL_ZERO, EmptyCString());

        // report the status and progress
        if (!mRestartInProgressVerifier.IsDiscardingContent())
            mActivityDistributor->ObserveActivity(
                mChannel,
                NS_HTTP_ACTIVITY_TYPE_SOCKET_TRANSPORT,
                static_cast<PRUint32>(status),
                PR_Now(),
                progress,
                EmptyCString());
    }

    // nsHttpChannel synthesizes progress events in OnDataAvailable
    if (status == nsISocketTransport::STATUS_RECEIVING_FROM)
        return;

    PRUint64 progressMax;

    if (status == nsISocketTransport::STATUS_SENDING_TO) {
        // suppress progress when only writing request headers
        if (!mHasRequestBody)
            return;

        nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mRequestStream);
        NS_ASSERTION(seekable, "Request stream isn't seekable?!?");

        PRInt64 prog = 0;
        seekable->Tell(&prog);
        progress = prog;

        // when uploading, we include the request headers in the progress
        // notifications.
        progressMax = mRequestSize; // XXX mRequestSize is 32-bit!
    }
    else {
        progress = LL_ZERO;
        progressMax = 0;
    }

    mTransportSink->OnTransportStatus(transport, status, progress, progressMax);
}

bool
nsHttpTransaction::IsDone()
{
    return mTransactionDone;
}

nsresult
nsHttpTransaction::Status()
{
    return mStatus;
}

PRUint8
nsHttpTransaction::Caps()
{ 
    return mCaps;
}

PRUint32
nsHttpTransaction::Available()
{
    PRUint32 size;
    if (NS_FAILED(mRequestStream->Available(&size)))
        size = 0;
    return size;
}

NS_METHOD
nsHttpTransaction::ReadRequestSegment(nsIInputStream *stream,
                                      void *closure,
                                      const char *buf,
                                      PRUint32 offset,
                                      PRUint32 count,
                                      PRUint32 *countRead)
{
    nsHttpTransaction *trans = (nsHttpTransaction *) closure;
    nsresult rv = trans->mReader->OnReadSegment(buf, count, countRead);
    if (NS_FAILED(rv)) return rv;

    if (trans->TimingEnabled() && trans->mTimings.requestStart.IsNull()) {
        // First data we're sending -> this is requestStart
        trans->mTimings.requestStart = mozilla::TimeStamp::Now();
    }
    trans->mSentData = true;
    return NS_OK;
}

nsresult
nsHttpTransaction::ReadSegments(nsAHttpSegmentReader *reader,
                                PRUint32 count, PRUint32 *countRead)
{
    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    if (mTransactionDone) {
        *countRead = 0;
        return mStatus;
    }

    if (!mConnected) {
        mConnected = true;
        mConnection->GetSecurityInfo(getter_AddRefs(mSecurityInfo));
    }

    mReader = reader;

    nsresult rv = mRequestStream->ReadSegments(ReadRequestSegment, this, count, countRead);

    mReader = nsnull;

    // if read would block then we need to AsyncWait on the request stream.
    // have callback occur on socket thread so we stay synchronized.
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        nsCOMPtr<nsIAsyncInputStream> asyncIn =
                do_QueryInterface(mRequestStream);
        if (asyncIn) {
            nsCOMPtr<nsIEventTarget> target;
            gHttpHandler->GetSocketThreadTarget(getter_AddRefs(target));
            if (target)
                asyncIn->AsyncWait(this, 0, 0, target);
            else {
                NS_ERROR("no socket thread event target");
                rv = NS_ERROR_UNEXPECTED;
            }
        }
    }

    return rv;
}

NS_METHOD
nsHttpTransaction::WritePipeSegment(nsIOutputStream *stream,
                                    void *closure,
                                    char *buf,
                                    PRUint32 offset,
                                    PRUint32 count,
                                    PRUint32 *countWritten)
{
    nsHttpTransaction *trans = (nsHttpTransaction *) closure;

    if (trans->mTransactionDone)
        return NS_BASE_STREAM_CLOSED; // stop iterating

    if (trans->TimingEnabled() && trans->mTimings.responseStart.IsNull()) {
        trans->mTimings.responseStart = mozilla::TimeStamp::Now();
    }

    nsresult rv;
    //
    // OK, now let the caller fill this segment with data.
    //
    rv = trans->mWriter->OnWriteSegment(buf, count, countWritten);
    if (NS_FAILED(rv)) return rv; // caller didn't want to write anything

    NS_ASSERTION(*countWritten > 0, "bad writer");
    trans->mReceivedData = true;

    // Let the transaction "play" with the buffer.  It is free to modify
    // the contents of the buffer and/or modify countWritten.
    // - Bytes in HTTP headers don't count towards countWritten, so the input
    // side of pipe (aka nsHttpChannel's mTransactionPump) won't hit
    // OnInputStreamReady until all headers have been parsed.
    //    
    rv = trans->ProcessData(buf, *countWritten, countWritten);
    if (NS_FAILED(rv))
        trans->Close(rv);

    return rv; // failure code only stops WriteSegments; it is not propagated.
}

nsresult
nsHttpTransaction::WriteSegments(nsAHttpSegmentWriter *writer,
                                 PRUint32 count, PRUint32 *countWritten)
{
    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    if (mTransactionDone)
        return NS_SUCCEEDED(mStatus) ? NS_BASE_STREAM_CLOSED : mStatus;

    mWriter = writer;

    nsresult rv = mPipeOut->WriteSegments(WritePipeSegment, this, count, countWritten);

    mWriter = nsnull;

    // if pipe would block then we need to AsyncWait on it.  have callback
    // occur on socket thread so we stay synchronized.
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        nsCOMPtr<nsIEventTarget> target;
        gHttpHandler->GetSocketThreadTarget(getter_AddRefs(target));
        if (target)
            mPipeOut->AsyncWait(this, 0, 0, target);
        else {
            NS_ERROR("no socket thread event target");
            rv = NS_ERROR_UNEXPECTED;
        }
    }

    return rv;
}

void
nsHttpTransaction::Close(nsresult reason)
{
    LOG(("nsHttpTransaction::Close [this=%x reason=%x]\n", this, reason));

    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    if (mClosed) {
        LOG(("  already closed\n"));
        return;
    }

    if (mActivityDistributor) {
        // report the reponse is complete if not already reported
        if (!mResponseIsComplete)
            mActivityDistributor->ObserveActivity(
                mChannel,
                NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
                NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_COMPLETE,
                PR_Now(),
                static_cast<PRUint64>(mContentRead),
                EmptyCString());

        // report that this transaction is closing
        mActivityDistributor->ObserveActivity(
            mChannel,
            NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
            NS_HTTP_ACTIVITY_SUBTYPE_TRANSACTION_CLOSE,
            PR_Now(), LL_ZERO, EmptyCString());
    }

    // we must no longer reference the connection!  find out if the 
    // connection was being reused before letting it go.
    bool connReused = false;
    if (mConnection)
        connReused = mConnection->IsReused();
    mConnected = false;

    //
    // if the connection was reset or closed before we wrote any part of the
    // request or if we wrote the request but didn't receive any part of the
    // response and the connection was being reused, then we can (and really
    // should) assume that we wrote to a stale connection and we must therefore
    // repeat the request over a new connection.
    //
    // NOTE: the conditions under which we will automatically retry the HTTP
    // request have to be carefully selected to avoid duplication of the
    // request from the point-of-view of the server.  such duplication could
    // have dire consequences including repeated purchases, etc.
    //
    // NOTE: because of the way SSL proxy CONNECT is implemented, it is
    // possible that the transaction may have received data without having
    // sent any data.  for this reason, mSendData == FALSE does not imply
    // mReceivedData == FALSE.  (see bug 203057 for more info.)
    //
    if (reason == NS_ERROR_NET_RESET || reason == NS_OK) {

        // reallySentData is meant to separate the instances where data has
        // been sent by this transaction but buffered at a higher level while
        // a TLS session (perhaps via a tunnel) is setup.
        bool reallySentData =
            mSentData && (!mConnection || mConnection->BytesWritten());
        
        if (!mReceivedData &&
            (!reallySentData || connReused || mPipelinePosition)) {
            // if restarting fails, then we must proceed to close the pipe,
            // which will notify the channel that the transaction failed.
            
            if (mPipelinePosition) {
                gHttpHandler->ConnMgr()->PipelineFeedbackInfo(
                    mConnInfo, nsHttpConnectionMgr::RedCanceledPipeline,
                    nsnull, 0);
            }
            if (NS_SUCCEEDED(Restart()))
                return;
        }
        else if (!mResponseIsComplete && mPipelinePosition &&
                 reason == NS_ERROR_NET_RESET) {
            // due to unhandled rst on a pipeline - safe to
            // restart as only idempotent is found there

            gHttpHandler->ConnMgr()->PipelineFeedbackInfo(
                mConnInfo, nsHttpConnectionMgr::RedCorruptedContent, nsnull, 0);
            if (NS_SUCCEEDED(RestartInProgress()))
                return;
        }
    }

    bool relConn = true;
    if (NS_SUCCEEDED(reason)) {
        if (!mResponseIsComplete) {
            // The response has not been delimited with a high-confidence
            // algorithm like Content-Length or Chunked Encoding. We
            // need to use a strong framing mechanism to pipeline.
            gHttpHandler->ConnMgr()->PipelineFeedbackInfo(
                mConnInfo, nsHttpConnectionMgr::BadInsufficientFraming,
                nsnull, mClassification);
        }
        else if (mPipelinePosition) {
            // report this success as feedback
            gHttpHandler->ConnMgr()->PipelineFeedbackInfo(
                mConnInfo, nsHttpConnectionMgr::GoodCompletedOK,
                nsnull, mPipelinePosition);
        }

        // the server has not sent the final \r\n terminating the header
        // section, and there may still be a header line unparsed.  let's make
        // sure we parse the remaining header line, and then hopefully, the
        // response will be usable (see bug 88792).
        if (!mHaveAllHeaders) {
            char data = '\n';
            PRUint32 unused;
            ParseHead(&data, 1, &unused);

            if (mResponseHead->Version() == NS_HTTP_VERSION_0_9) {
                // Reject 0 byte HTTP/0.9 Responses - bug 423506
                LOG(("nsHttpTransaction::Close %p 0 Byte 0.9 Response", this));
                reason = NS_ERROR_NET_RESET;
            }
        }

        // honor the sticky connection flag...
        if (mCaps & NS_HTTP_STICKY_CONNECTION)
            relConn = false;
    }

    // mTimings.responseEnd is normally recorded based on the end of a
    // HTTP delimiter such as chunked-encodings or content-length. However,
    // EOF or an error still require an end time be recorded.
    if (TimingEnabled() &&
        mTimings.responseEnd.IsNull() && !mTimings.responseStart.IsNull())
        mTimings.responseEnd = mozilla::TimeStamp::Now();

    if (relConn && mConnection)
        NS_RELEASE(mConnection);

    mStatus = reason;
    mTransactionDone = true; // forcibly flag the transaction as complete
    mClosed = true;

    // release some resources that we no longer need
    mRequestStream = nsnull;
    mReqHeaderBuf.Truncate();
    mLineBuf.Truncate();
    if (mChunkedDecoder) {
        delete mChunkedDecoder;
        mChunkedDecoder = nsnull;
    }

    // closing this pipe triggers the channel's OnStopRequest method.
    mPipeOut->CloseWithStatus(reason);
}

nsresult
nsHttpTransaction::AddTransaction(nsAHttpTransaction *trans)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

PRUint32
nsHttpTransaction::PipelineDepth()
{
    return IsDone() ? 0 : 1;
}

nsresult
nsHttpTransaction::SetPipelinePosition(PRInt32 position)
{
    mPipelinePosition = position;
    return NS_OK;
}
 
PRInt32
nsHttpTransaction::PipelinePosition()
{
    return mPipelinePosition;
}

//-----------------------------------------------------------------------------
// nsHttpTransaction <private>
//-----------------------------------------------------------------------------

nsresult
nsHttpTransaction::RestartInProgress()
{
    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");
    
    if ((mRestartCount + 1) >= gHttpHandler->MaxRequestAttempts()) {
        LOG(("nsHttpTransaction::RestartInProgress() "
             "reached max request attempts, failing transaction %p\n", this));
        return NS_ERROR_NET_RESET;
    }

    // Lock RestartInProgress() and TakeResponseHead() against main thread
    MutexAutoLock lock(*nsHttp::GetLock());

    // Don't try and RestartInProgress() things that haven't gotten a response
    // header yet. Those should be handled under the normal restart() path if
    // they are eligible.
    if (!mHaveAllHeaders)
        return NS_ERROR_NET_RESET;

    // don't try and restart 0.9 or non 200/Get HTTP/1
    if (!mRestartInProgressVerifier.IsSetup())
        return NS_ERROR_NET_RESET;

    LOG(("Will restart transaction %p and skip first %lld bytes, "
         "old Content-Length %lld",
         this, mContentRead, mContentLength));

    mRestartInProgressVerifier.SetAlreadyProcessed(
        PR_MAX(mRestartInProgressVerifier.AlreadyProcessed(), mContentRead));

    if (!mResponseHeadTaken && !mForTakeResponseHead) {
        // TakeResponseHeader() has not been called yet and this
        // is the first restart. Store the resp headers exclusively
        // for TakeResponseHead() which is called from the main thread and
        // could happen at any time - so we can't continue to modify those
        // headers (which restarting will effectively do)
        mForTakeResponseHead = mResponseHead;
        mResponseHead = nsnull;
    }

    if (mResponseHead) {
        mResponseHead->Reset();
    }

    mContentRead = 0;
    mContentLength = -1;
    delete mChunkedDecoder;
    mChunkedDecoder = nsnull;
    mHaveStatusLine = false;
    mHaveAllHeaders = false;
    mHttpResponseMatched = false;
    mResponseIsComplete = false;
    mDidContentStart = false;
    mNoContent = false;
    mSentData = false;
    mReceivedData = false;

    return Restart();
}

nsresult
nsHttpTransaction::Restart()
{
    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    // limit the number of restart attempts - bug 92224
    if (++mRestartCount >= gHttpHandler->MaxRequestAttempts()) {
        LOG(("reached max request attempts, failing transaction @%x\n", this));
        return NS_ERROR_NET_RESET;
    }

    LOG(("restarting transaction @%x\n", this));

    // rewind streams in case we already wrote out the request
    nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mRequestStream);
    if (seekable)
        seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);

    // clear old connection state...
    mSecurityInfo = 0;
    NS_IF_RELEASE(mConnection);

    // disable pipelining for the next attempt in case pipelining caused the
    // reset.  this is being overly cautious since we don't know if pipelining
    // was the problem here.
    mCaps &= ~NS_HTTP_ALLOW_PIPELINING;

    return gHttpHandler->InitiateTransaction(this, mPriority);
}

char *
nsHttpTransaction::LocateHttpStart(char *buf, PRUint32 len,
                                   bool aAllowPartialMatch)
{
    NS_ASSERTION(!aAllowPartialMatch || mLineBuf.IsEmpty(), "ouch");

    static const char HTTPHeader[] = "HTTP/1.";
    static const PRUint32 HTTPHeaderLen = sizeof(HTTPHeader) - 1;
    static const char HTTP2Header[] = "HTTP/2.0";
    static const PRUint32 HTTP2HeaderLen = sizeof(HTTP2Header) - 1;
    
    if (aAllowPartialMatch && (len < HTTPHeaderLen))
        return (PL_strncasecmp(buf, HTTPHeader, len) == 0) ? buf : nsnull;

    // mLineBuf can contain partial match from previous search
    if (!mLineBuf.IsEmpty()) {
        NS_ASSERTION(mLineBuf.Length() < HTTPHeaderLen, "ouch");
        PRInt32 checkChars = NS_MIN(len, HTTPHeaderLen - mLineBuf.Length());
        if (PL_strncasecmp(buf, HTTPHeader + mLineBuf.Length(),
                           checkChars) == 0) {
            mLineBuf.Append(buf, checkChars);
            if (mLineBuf.Length() == HTTPHeaderLen) {
                // We've found whole HTTPHeader sequence. Return pointer at the
                // end of matched sequence since it is stored in mLineBuf.
                return (buf + checkChars);
            }
            // Response matches pattern but is still incomplete.
            return 0;
        }
        // Previous partial match together with new data doesn't match the
        // pattern. Start the search again.
        mLineBuf.Truncate();
    }

    bool firstByte = true;
    while (len > 0) {
        if (PL_strncasecmp(buf, HTTPHeader, NS_MIN<PRUint32>(len, HTTPHeaderLen)) == 0) {
            if (len < HTTPHeaderLen) {
                // partial HTTPHeader sequence found
                // save partial match to mLineBuf
                mLineBuf.Assign(buf, len);
                return 0;
            }

            // whole HTTPHeader sequence found
            return buf;
        }

        // At least "SmarterTools/2.0.3974.16813" generates nonsensical
        // HTTP/2.0 responses to our HTTP/1 requests. Treat the minimal case of
        // it as HTTP/1.1 to be compatible with old versions of ourselves and
        // other browsers

        if (firstByte && !mInvalidResponseBytesRead && len >= HTTP2HeaderLen &&
            (PL_strncasecmp(buf, HTTP2Header, HTTP2HeaderLen) == 0)) {
            LOG(("nsHttpTransaction:: Identified HTTP/2.0 treating as 1.x\n"));
            return buf;
        }

        if (!nsCRT::IsAsciiSpace(*buf))
            firstByte = false;
        buf++;
        len--;
    }
    return 0;
}

nsresult
nsHttpTransaction::ParseLine(char *line)
{
    LOG(("nsHttpTransaction::ParseLine [%s]\n", line));
    nsresult rv = NS_OK;
    
    if (!mHaveStatusLine) {
        mResponseHead->ParseStatusLine(line);
        mHaveStatusLine = true;
        // XXX this should probably never happen
        if (mResponseHead->Version() == NS_HTTP_VERSION_0_9)
            mHaveAllHeaders = true;
    }
    else {
        rv = mResponseHead->ParseHeaderLine(line);
    }
    return rv;
}

nsresult
nsHttpTransaction::ParseLineSegment(char *segment, PRUint32 len)
{
    NS_PRECONDITION(!mHaveAllHeaders, "already have all headers");

    if (!mLineBuf.IsEmpty() && mLineBuf.Last() == '\n') {
        // trim off the new line char, and if this segment is
        // not a continuation of the previous or if we haven't
        // parsed the status line yet, then parse the contents
        // of mLineBuf.
        mLineBuf.Truncate(mLineBuf.Length() - 1);
        if (!mHaveStatusLine || (*segment != ' ' && *segment != '\t')) {
            nsresult rv = ParseLine(mLineBuf.BeginWriting());
            mLineBuf.Truncate();
            if (NS_FAILED(rv)) {
                gHttpHandler->ConnMgr()->PipelineFeedbackInfo(
                    mConnInfo, nsHttpConnectionMgr::RedCorruptedContent,
                    nsnull, 0);
                return rv;
            }
        }
    }

    // append segment to mLineBuf...
    mLineBuf.Append(segment, len);
    
    // a line buf with only a new line char signifies the end of headers.
    if (mLineBuf.First() == '\n') {
        mLineBuf.Truncate();
        // discard this response if it is a 100 continue or other 1xx status.
        PRUint16 status = mResponseHead->Status();
        if ((status != 101) && (status / 100 == 1)) {
            LOG(("ignoring 1xx response\n"));
            mHaveStatusLine = false;
            mHttpResponseMatched = false;
            mConnection->SetLastTransactionExpectedNoContent(true);
            mResponseHead->Reset();
            return NS_OK;
        }
        mHaveAllHeaders = true;
    }
    return NS_OK;
}

nsresult
nsHttpTransaction::ParseHead(char *buf,
                             PRUint32 count,
                             PRUint32 *countRead)
{
    nsresult rv;
    PRUint32 len;
    char *eol;

    LOG(("nsHttpTransaction::ParseHead [count=%u]\n", count));

    *countRead = 0;

    NS_PRECONDITION(!mHaveAllHeaders, "oops");
        
    // allocate the response head object if necessary
    if (!mResponseHead) {
        mResponseHead = new nsHttpResponseHead();
        if (!mResponseHead)
            return NS_ERROR_OUT_OF_MEMORY;

        // report that we have a least some of the response
        if (mActivityDistributor && !mReportedStart) {
            mReportedStart = true;
            mActivityDistributor->ObserveActivity(
                mChannel,
                NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
                NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_START,
                PR_Now(), LL_ZERO, EmptyCString());
        }
    }

    if (!mHttpResponseMatched) {
        // Normally we insist on seeing HTTP/1.x in the first few bytes,
        // but if we are on a persistent connection and the previous transaction
        // was not supposed to have any content then we need to be prepared
        // to skip over a response body that the server may have sent even
        // though it wasn't allowed.
        if (!mConnection || !mConnection->LastTransactionExpectedNoContent()) {
            // tolerate only minor junk before the status line
            mHttpResponseMatched = true;
            char *p = LocateHttpStart(buf, NS_MIN<PRUint32>(count, 11), true);
            if (!p) {
                // Treat any 0.9 style response of a put as a failure.
                if (mRequestHead->Method() == nsHttp::Put)
                    return NS_ERROR_ABORT;

                mResponseHead->ParseStatusLine("");
                mHaveStatusLine = true;
                mHaveAllHeaders = true;
                return NS_OK;
            }
            if (p > buf) {
                // skip over the junk
                mInvalidResponseBytesRead += p - buf;
                *countRead = p - buf;
                buf = p;
            }
        }
        else {
            char *p = LocateHttpStart(buf, count, false);
            if (p) {
                mInvalidResponseBytesRead += p - buf;
                *countRead = p - buf;
                buf = p;
                mHttpResponseMatched = true;
            } else {
                mInvalidResponseBytesRead += count;
                *countRead = count;
                if (mInvalidResponseBytesRead > MAX_INVALID_RESPONSE_BODY_SIZE) {
                    LOG(("nsHttpTransaction::ParseHead() "
                         "Cannot find Response Header\n"));
                    // cannot go back and call this 0.9 anymore as we
                    // have thrown away a lot of the leading junk
                    return NS_ERROR_ABORT;
                }
                return NS_OK;
            }
        }
    }
    // otherwise we can assume that we don't have a HTTP/0.9 response.

    NS_ABORT_IF_FALSE (mHttpResponseMatched, "inconsistent");
    while ((eol = static_cast<char *>(memchr(buf, '\n', count - *countRead))) != nsnull) {
        // found line in range [buf:eol]
        len = eol - buf + 1;

        *countRead += len;

        // actually, the line is in the range [buf:eol-1]
        if ((eol > buf) && (*(eol-1) == '\r'))
            len--;

        buf[len-1] = '\n';
        rv = ParseLineSegment(buf, len);
        if (NS_FAILED(rv))
            return rv;

        if (mHaveAllHeaders)
            return NS_OK;

        // skip over line
        buf = eol + 1;

        if (!mHttpResponseMatched) {
            // a 100 class response has caused us to throw away that set of
            // response headers and look for the next response
            return NS_ERROR_NET_INTERRUPT;
        }
    }

    // do something about a partial header line
    if (!mHaveAllHeaders && (len = count - *countRead)) {
        *countRead = count;
        // ignore a trailing carriage return, and don't bother calling
        // ParseLineSegment if buf only contains a carriage return.
        if ((buf[len-1] == '\r') && (--len == 0))
            return NS_OK;
        rv = ParseLineSegment(buf, len);
        if (NS_FAILED(rv))
            return rv;
    }
    return NS_OK;
}

// called on the socket thread
nsresult
nsHttpTransaction::HandleContentStart()
{
    LOG(("nsHttpTransaction::HandleContentStart [this=%x]\n", this));

    if (mResponseHead) {
#if defined(PR_LOGGING)
        if (LOG3_ENABLED()) {
            LOG3(("http response [\n"));
            nsCAutoString headers;
            mResponseHead->Flatten(headers, false);
            LogHeaders(headers.get());
            LOG3(("]\n"));
        }
#endif
        // notify the connection, give it a chance to cause a reset.
        bool reset = false;
        if (!mRestartInProgressVerifier.IsSetup())
            mConnection->OnHeadersAvailable(this, mRequestHead, mResponseHead, &reset);

        // looks like we should ignore this response, resetting...
        if (reset) {
            LOG(("resetting transaction's response head\n"));
            mHaveAllHeaders = false;
            mHaveStatusLine = false;
            mReceivedData = false;
            mSentData = false;
            mHttpResponseMatched = false;
            mResponseHead->Reset();
            // wait to be called again...
            return NS_OK;
        }

        // check if this is a no-content response
        switch (mResponseHead->Status()) {
        case 101:
            mPreserveStream = true;    // fall through to other no content
        case 204:
        case 205:
        case 304:
            mNoContent = true;
            LOG(("this response should not contain a body.\n"));
            break;
        }
        
        if (mResponseHead->Status() == 200 &&
            mConnection->IsProxyConnectInProgress()) {
            // successful CONNECTs do not have response bodies
            mNoContent = true;
        }
        mConnection->SetLastTransactionExpectedNoContent(mNoContent);
        if (mInvalidResponseBytesRead)
            gHttpHandler->ConnMgr()->PipelineFeedbackInfo(
                mConnInfo, nsHttpConnectionMgr::BadInsufficientFraming,
                nsnull, mClassification);

        if (mNoContent)
            mContentLength = 0;
        else {
            // grab the content-length from the response headers
            mContentLength = mResponseHead->ContentLength();

            if ((mClassification != CLASS_SOLO) &&
                (mContentLength > mMaxPipelineObjectSize))
                CancelPipeline(nsHttpConnectionMgr::BadUnexpectedLarge);
            
            // handle chunked encoding here, so we'll know immediately when
            // we're done with the socket.  please note that _all_ other
            // decoding is done when the channel receives the content data
            // so as not to block the socket transport thread too much.
            // ignore chunked responses from HTTP/1.0 servers and proxies.
            if (mResponseHead->Version() >= NS_HTTP_VERSION_1_1 &&
                mResponseHead->HasHeaderValue(nsHttp::Transfer_Encoding, "chunked")) {
                // we only support the "chunked" transfer encoding right now.
                mChunkedDecoder = new nsHttpChunkedDecoder();
                if (!mChunkedDecoder)
                    return NS_ERROR_OUT_OF_MEMORY;
                LOG(("chunked decoder created\n"));
                // Ignore server specified Content-Length.
                mContentLength = -1;
            }
#if defined(PR_LOGGING)
            else if (mContentLength == PRInt64(-1))
                LOG(("waiting for the server to close the connection.\n"));
#endif
        }
        if (mRestartInProgressVerifier.IsSetup() &&
            !mRestartInProgressVerifier.Verify(mContentLength, mResponseHead)) {
            LOG(("Restart in progress subsequent transaction failed to match"));
            return NS_ERROR_ABORT;
        }
    }

    mDidContentStart = true;

    // The verifier only initializes itself once (from the first iteration of
    // a transaction that gets far enough to have response headers)
    if (mRequestHead->Method() == nsHttp::Get)
        mRestartInProgressVerifier.Set(mContentLength, mResponseHead);

    return NS_OK;
}

// called on the socket thread
nsresult
nsHttpTransaction::HandleContent(char *buf,
                                 PRUint32 count,
                                 PRUint32 *contentRead,
                                 PRUint32 *contentRemaining)
{
    nsresult rv;

    LOG(("nsHttpTransaction::HandleContent [this=%x count=%u]\n", this, count));

    *contentRead = 0;
    *contentRemaining = 0;

    NS_ASSERTION(mConnection, "no connection");

    if (!mDidContentStart) {
        rv = HandleContentStart();
        if (NS_FAILED(rv)) return rv;
        // Do not write content to the pipe if we haven't started streaming yet
        if (!mDidContentStart)
            return NS_OK;
    }

    if (mChunkedDecoder) {
        // give the buf over to the chunked decoder so it can reformat the
        // data and tell us how much is really there.
        rv = mChunkedDecoder->HandleChunkedContent(buf, count, contentRead, contentRemaining);
        if (NS_FAILED(rv)) return rv;
    }
    else if (mContentLength >= PRInt64(0)) {
        // HTTP/1.0 servers have been known to send erroneous Content-Length
        // headers. So, unless the connection is persistent, we must make
        // allowances for a possibly invalid Content-Length header. Thus, if
        // NOT persistent, we simply accept everything in |buf|.
        if (mConnection->IsPersistent() || mPreserveStream) {
            PRInt64 remaining = mContentLength - mContentRead;
            *contentRead = PRUint32(NS_MIN<PRInt64>(count, remaining));
            *contentRemaining = count - *contentRead;
        }
        else {
            *contentRead = count;
            // mContentLength might need to be increased...
            PRInt64 position = mContentRead + PRInt64(count);
            if (position > mContentLength) {
                mContentLength = position;
                //mResponseHead->SetContentLength(mContentLength);
            }
        }
    }
    else {
        // when we are just waiting for the server to close the connection...
        // (no explicit content-length given)
        *contentRead = count;
    }
    
    PRInt64 toReadBeforeRestart =
        mRestartInProgressVerifier.ToReadBeforeRestart();

    if (toReadBeforeRestart && *contentRead) {
        PRUint32 ignore =
            PR_MIN(toReadBeforeRestart, PR_UINT32_MAX);
        ignore = PR_MIN(*contentRead, ignore);
        LOG(("Due To Restart ignoring %d of remaining %ld",
             ignore, toReadBeforeRestart));
        *contentRead -= ignore;
        mContentRead += ignore;
        mRestartInProgressVerifier.HaveReadBeforeRestart(ignore);
        memmove(buf, buf + ignore, *contentRead + *contentRemaining);
    }

    if (*contentRead) {
        // update count of content bytes read and report progress...
        mContentRead += *contentRead;
        /* when uncommenting, take care of 64-bit integers w/ NS_MAX...
        if (mProgressSink)
            mProgressSink->OnProgress(nsnull, nsnull, mContentRead, NS_MAX(0, mContentLength));
        */
    }

    LOG(("nsHttpTransaction::HandleContent [this=%x count=%u read=%u mContentRead=%lld mContentLength=%lld]\n",
        this, count, *contentRead, mContentRead, mContentLength));

    // Check the size of chunked responses. If we exceed the max pipeline size
    // for this response reschedule the pipeline
    if ((mClassification != CLASS_SOLO) &&
        mChunkedDecoder &&
        ((mContentRead + mChunkedDecoder->GetChunkRemaining()) >
         mMaxPipelineObjectSize)) {
        CancelPipeline(nsHttpConnectionMgr::BadUnexpectedLarge);
    }

    // check for end-of-file
    if ((mContentRead == mContentLength) ||
        (mChunkedDecoder && mChunkedDecoder->ReachedEOF())) {
        // the transaction is done with a complete response.
        mTransactionDone = true;
        mResponseIsComplete = true;

        if (TimingEnabled())
            mTimings.responseEnd = mozilla::TimeStamp::Now();

        // report the entire response has arrived
        if (mActivityDistributor)
            mActivityDistributor->ObserveActivity(
                mChannel,
                NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
                NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_COMPLETE,
                PR_Now(),
                static_cast<PRUint64>(mContentRead),
                EmptyCString());
    }

    return NS_OK;
}

nsresult
nsHttpTransaction::ProcessData(char *buf, PRUint32 count, PRUint32 *countRead)
{
    nsresult rv;

    LOG(("nsHttpTransaction::ProcessData [this=%x count=%u]\n", this, count));

    *countRead = 0;

    // we may not have read all of the headers yet...
    if (!mHaveAllHeaders) {
        PRUint32 bytesConsumed = 0;

        do {
            PRUint32 localBytesConsumed = 0;
            char *localBuf = buf + bytesConsumed;
            PRUint32 localCount = count - bytesConsumed;
            
            rv = ParseHead(localBuf, localCount, &localBytesConsumed);
            if (NS_FAILED(rv) && rv != NS_ERROR_NET_INTERRUPT)
                return rv;
            bytesConsumed += localBytesConsumed;
        } while (rv == NS_ERROR_NET_INTERRUPT);
        
        count -= bytesConsumed;

        // if buf has some content in it, shift bytes to top of buf.
        if (count && bytesConsumed)
            memmove(buf, buf + bytesConsumed, count);

        // report the completed response header
        if (mActivityDistributor && mResponseHead && mHaveAllHeaders &&
            !mReportedResponseHeader) {
            mReportedResponseHeader = true;
            nsCAutoString completeResponseHeaders;
            mResponseHead->Flatten(completeResponseHeaders, false);
            completeResponseHeaders.AppendLiteral("\r\n");
            mActivityDistributor->ObserveActivity(
                mChannel,
                NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
                NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_HEADER,
                PR_Now(), LL_ZERO,
                completeResponseHeaders);
        }
    }

    // even though count may be 0, we still want to call HandleContent
    // so it can complete the transaction if this is a "no-content" response.
    if (mHaveAllHeaders) {
        PRUint32 countRemaining = 0;
        //
        // buf layout:
        // 
        // +--------------------------------------+----------------+-----+
        // |              countRead               | countRemaining |     |
        // +--------------------------------------+----------------+-----+
        //
        // count          : bytes read from the socket
        // countRead      : bytes corresponding to this transaction
        // countRemaining : bytes corresponding to next pipelined transaction
        //
        // NOTE:
        // count > countRead + countRemaining <==> chunked transfer encoding
        //
        rv = HandleContent(buf, count, countRead, &countRemaining);
        if (NS_FAILED(rv)) return rv;
        // we may have read more than our share, in which case we must give
        // the excess bytes back to the connection
        if (mResponseIsComplete && countRemaining) {
            NS_ASSERTION(mConnection, "no connection");
            mConnection->PushBack(buf + *countRead, countRemaining);
        }
    }

    return NS_OK;
}

void
nsHttpTransaction::CancelPipeline(PRUint32 reason)
{
    // reason is casted through a uint to avoid compiler header deps
    gHttpHandler->ConnMgr()->PipelineFeedbackInfo(
        mConnInfo,
        static_cast<nsHttpConnectionMgr::PipelineFeedbackInfoType>(reason),
        nsnull, mClassification);

    mConnection->CancelPipeline(NS_ERROR_ABORT);

    // Avoid pipelining this transaction on restart by classifying it as solo.
    // This also prevents BadUnexpectedLarge from being reported more
    // than one time per transaction.
    mClassification = CLASS_SOLO;
}

//-----------------------------------------------------------------------------
// nsHttpTransaction deletion event
//-----------------------------------------------------------------------------

class nsDeleteHttpTransaction : public nsRunnable {
public:
    nsDeleteHttpTransaction(nsHttpTransaction *trans)
        : mTrans(trans)
    {}

    NS_IMETHOD Run()
    {
        delete mTrans;
        return NS_OK;
    }
private:
    nsHttpTransaction *mTrans;
};

void
nsHttpTransaction::DeleteSelfOnConsumerThread()
{
    LOG(("nsHttpTransaction::DeleteSelfOnConsumerThread [this=%x]\n", this));
    
    bool val;
    if (!mConsumerTarget ||
        (NS_SUCCEEDED(mConsumerTarget->IsOnCurrentThread(&val)) && val)) {
        delete this;
    } else {
        LOG(("proxying delete to consumer thread...\n"));
        nsCOMPtr<nsIRunnable> event = new nsDeleteHttpTransaction(this);
        if (NS_FAILED(mConsumerTarget->Dispatch(event, NS_DISPATCH_NORMAL)))
            NS_WARNING("failed to dispatch nsHttpDeleteTransaction event");
    }
}

//-----------------------------------------------------------------------------
// nsHttpTransaction::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_THREADSAFE_ADDREF(nsHttpTransaction)

NS_IMETHODIMP_(nsrefcnt)
nsHttpTransaction::Release()
{
    nsrefcnt count;
    NS_PRECONDITION(0 != mRefCnt, "dup release");
    count = NS_AtomicDecrementRefcnt(mRefCnt);
    NS_LOG_RELEASE(this, count, "nsHttpTransaction");
    if (0 == count) {
        mRefCnt = 1; /* stablize */
        // it is essential that the transaction be destroyed on the consumer 
        // thread (we could be holding the last reference to our consumer).
        DeleteSelfOnConsumerThread();
        return 0;
    }
    return count;
}

NS_IMPL_THREADSAFE_QUERY_INTERFACE2(nsHttpTransaction,
                                    nsIInputStreamCallback,
                                    nsIOutputStreamCallback)

//-----------------------------------------------------------------------------
// nsHttpTransaction::nsIInputStreamCallback
//-----------------------------------------------------------------------------

// called on the socket thread
NS_IMETHODIMP
nsHttpTransaction::OnInputStreamReady(nsIAsyncInputStream *out)
{
    if (mConnection) {
        mConnection->TransactionHasDataToWrite(this);
        nsresult rv = mConnection->ResumeSend();
        if (NS_FAILED(rv))
            NS_ERROR("ResumeSend failed");
    }
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpTransaction::nsIOutputStreamCallback
//-----------------------------------------------------------------------------

// called on the socket thread
NS_IMETHODIMP
nsHttpTransaction::OnOutputStreamReady(nsIAsyncOutputStream *out)
{
    if (mConnection) {
        nsresult rv = mConnection->ResumeRecv();
        if (NS_FAILED(rv))
            NS_ERROR("ResumeRecv failed");
    }
    return NS_OK;
}

// nsHttpTransaction::RestartVerifier

static bool
matchOld(nsHttpResponseHead *newHead, nsCString &old,
         nsHttpAtom headerAtom)
{
    const char *val;
    
    val = newHead->PeekHeader(headerAtom);
    if (val && old.IsEmpty())
        return false;
    if (!val && !old.IsEmpty())
        return false;
    if (val && !old.Equals(val))
        return false;
    return true;
}

bool
nsHttpTransaction::RestartVerifier::Verify(PRInt64 contentLength,
                                           nsHttpResponseHead *newHead)
{
    if (mContentLength != contentLength)
        return false;

    if (newHead->Status() != 200)
        return false;

    if (!matchOld(newHead, mContentRange, nsHttp::Content_Range))
        return false;

    if (!matchOld(newHead, mLastModified, nsHttp::Last_Modified))
        return false;

    if (!matchOld(newHead, mETag, nsHttp::ETag))
        return false;

    if (!matchOld(newHead, mContentEncoding, nsHttp::Content_Encoding))
        return false;

    if (!matchOld(newHead, mTransferEncoding, nsHttp::Transfer_Encoding))
        return false;
    
    return true;
}

void
nsHttpTransaction::RestartVerifier::Set(PRInt64 contentLength,
                                        nsHttpResponseHead *head)
{
    if (mSetup)
        return;

    // If mSetup does not transition to true RestartInPogress() is later
    // forbidden

    // Only RestartInProgress with 200 response code
    if (head->Status() != 200)
        return;

    mContentLength = contentLength;
    
    if (head) {
        const char *val;
        val = head->PeekHeader(nsHttp::ETag);
        if (val)
            mETag.Assign(val);
        val = head->PeekHeader(nsHttp::Last_Modified);
        if (val)
            mLastModified.Assign(val);
        val = head->PeekHeader(nsHttp::Content_Range);
        if (val)
            mContentRange.Assign(val);
        val = head->PeekHeader(nsHttp::Content_Encoding);
        if (val)
            mContentEncoding.Assign(val);
        val = head->PeekHeader(nsHttp::Transfer_Encoding);
        if (val)
            mTransferEncoding.Assign(val);

        // We can only restart with any confidence if we have a stored etag or
        // last-modified header
        if (mETag.IsEmpty() && mLastModified.IsEmpty())
            return;

        mSetup = true;
    }
}
