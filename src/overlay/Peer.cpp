// Copyright 2014 EssaBlockChain Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/Peer.h"

#include "BanManager.h"
#include "crypto/Hex.h"
#include "crypto/Random.h"
#include "crypto/SHA.h"
#include "database/Database.h"
#include "herder/Herder.h"
#include "herder/TxSetFrame.h"
#include "ledger/LedgerManager.h"
#include "main/Application.h"
#include "main/Config.h"
#include "overlay/LoadManager.h"
#include "overlay/OverlayManager.h"
#include "overlay/PeerAuth.h"
#include "overlay/PeerManager.h"
#include "overlay/EssaBlockChainXDR.h"
#include "util/Logging.h"
#include "util/XDROperators.h"

#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "medida/timer.h"

#include "xdrpp/marshal.h"

#include <soci.h>
#include <time.h>

// LATER: need to add some way of docking peers that are misbehaving by sending
// you bad data

namespace essa
{

using namespace std;
using namespace soci;

medida::Meter&
Peer::getByteReadMeter(Application& app)
{
    return app.getMetrics().NewMeter({"overlay", "byte", "read"}, "byte");
}

medida::Meter&
Peer::getByteWriteMeter(Application& app)
{
    return app.getMetrics().NewMeter({"overlay", "byte", "write"}, "byte");
}

Peer::Peer(Application& app, PeerRole role)
    : mApp(app)
    , mRole(role)
    , mState(role == WE_CALLED_REMOTE ? CONNECTING : CONNECTED)
    , mRemoteOverlayVersion(0)
    , mIdleTimer(app)
    , mLastRead(app.getClock().now())
    , mLastWrite(app.getClock().now())
    , mLastEmpty(app.getClock().now())

    , mMessageRead(
          app.getMetrics().NewMeter({"overlay", "message", "read"}, "message"))
    , mMessageWrite(
          app.getMetrics().NewMeter({"overlay", "message", "write"}, "message"))
    , mByteRead(getByteReadMeter(app))
    , mByteWrite(getByteWriteMeter(app))
    , mErrorRead(
          app.getMetrics().NewMeter({"overlay", "error", "read"}, "error"))
    , mErrorWrite(
          app.getMetrics().NewMeter({"overlay", "error", "write"}, "error"))
    , mTimeoutIdle(
          app.getMetrics().NewMeter({"overlay", "timeout", "idle"}, "timeout"))
    , mTimeoutStraggler(app.getMetrics().NewMeter(
          {"overlay", "timeout", "straggler"}, "timeout"))

    , mRecvErrorTimer(app.getMetrics().NewTimer({"overlay", "recv", "error"}))
    , mRecvHelloTimer(app.getMetrics().NewTimer({"overlay", "recv", "hello"}))
    , mRecvAuthTimer(app.getMetrics().NewTimer({"overlay", "recv", "auth"}))
    , mRecvDontHaveTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "dont-have"}))
    , mRecvGetPeersTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "get-peers"}))
    , mRecvPeersTimer(app.getMetrics().NewTimer({"overlay", "recv", "peers"}))
    , mRecvGetTxSetTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "get-txset"}))
    , mRecvTxSetTimer(app.getMetrics().NewTimer({"overlay", "recv", "txset"}))
    , mRecvTransactionTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "transaction"}))
    , mRecvGetSCPQuorumSetTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "get-scp-qset"}))
    , mRecvSCPQuorumSetTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-qset"}))
    , mRecvSCPMessageTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-message"}))
    , mRecvGetSCPStateTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "get-scp-state"}))

    , mRecvSCPPrepareTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-prepare"}))
    , mRecvSCPConfirmTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-confirm"}))
    , mRecvSCPNominateTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-nominate"}))
    , mRecvSCPExternalizeTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-externalize"}))

    , mSendErrorMeter(
          app.getMetrics().NewMeter({"overlay", "send", "error"}, "message"))
    , mSendHelloMeter(
          app.getMetrics().NewMeter({"overlay", "send", "hello"}, "message"))
    , mSendAuthMeter(
          app.getMetrics().NewMeter({"overlay", "send", "auth"}, "message"))
    , mSendDontHaveMeter(app.getMetrics().NewMeter(
          {"overlay", "send", "dont-have"}, "message"))
    , mSendGetPeersMeter(app.getMetrics().NewMeter(
          {"overlay", "send", "get-peers"}, "message"))
    , mSendPeersMeter(
          app.getMetrics().NewMeter({"overlay", "send", "peers"}, "message"))
    , mSendGetTxSetMeter(app.getMetrics().NewMeter(
          {"overlay", "send", "get-txset"}, "message"))
    , mSendTransactionMeter(app.getMetrics().NewMeter(
          {"overlay", "send", "transaction"}, "message"))
    , mSendTxSetMeter(
          app.getMetrics().NewMeter({"overlay", "send", "txset"}, "message"))
    , mSendGetSCPQuorumSetMeter(app.getMetrics().NewMeter(
          {"overlay", "send", "get-scp-qset"}, "message"))
    , mSendSCPQuorumSetMeter(
          app.getMetrics().NewMeter({"overlay", "send", "scp-qset"}, "message"))
    , mSendSCPMessageSetMeter(app.getMetrics().NewMeter(
          {"overlay", "send", "scp-message"}, "message"))
    , mSendGetSCPStateMeter(app.getMetrics().NewMeter(
          {"overlay", "send", "get-scp-state"}, "message"))
{
    auto bytes = randomBytes(mSendNonce.size());
    std::copy(bytes.begin(), bytes.end(), mSendNonce.begin());
}

void
Peer::sendHello()
{
    CLOG(DEBUG, "Overlay") << "Peer::sendHello to " << toString();
    EssaBlockChainMessage msg;
    msg.type(HELLO);
    Hello& elo = msg.hello();
    elo.ledgerVersion = mApp.getConfig().LEDGER_PROTOCOL_VERSION;
    elo.overlayMinVersion = mApp.getConfig().OVERLAY_PROTOCOL_MIN_VERSION;
    elo.overlayVersion = mApp.getConfig().OVERLAY_PROTOCOL_VERSION;
    elo.versionStr = mApp.getConfig().VERSION_STR;
    elo.networkID = mApp.getNetworkID();
    elo.listeningPort = mApp.getConfig().PEER_PORT;
    elo.peerID = mApp.getConfig().NODE_SEED.getPublicKey();
    elo.cert = this->getAuthCert();
    elo.nonce = mSendNonce;
    sendMessage(msg);
}

AuthCert
Peer::getAuthCert()
{
    return mApp.getOverlayManager().getPeerAuth().getAuthCert();
}

std::chrono::seconds
Peer::getIOTimeout() const
{
    if (isAuthenticated())
    {
        // Normally willing to wait 30s to hear anything
        // from an authenticated peer.
        return std::chrono::seconds(mApp.getConfig().PEER_TIMEOUT);
    }
    else
    {
        // We give peers much less timing leeway while
        // performing handshake.
        return std::chrono::seconds(
            mApp.getConfig().PEER_AUTHENTICATION_TIMEOUT);
    }
}

void
Peer::receivedBytes(size_t byteCount, bool gotFullMessage)
{
    if (shouldAbort())
    {
        return;
    }

    LoadManager::PeerContext loadCtx(mApp, mPeerID);
    mLastRead = mApp.getClock().now();
    if (gotFullMessage)
        mMessageRead.Mark();
    mByteRead.Mark(byteCount);
}

void
Peer::startIdleTimer()
{
    if (shouldAbort())
    {
        return;
    }

    auto self = shared_from_this();
    mIdleTimer.expires_from_now(getIOTimeout());
    mIdleTimer.async_wait([self](asio::error_code const& error) {
        self->idleTimerExpired(error);
    });
}

void
Peer::idleTimerExpired(asio::error_code const& error)
{
    if (!error)
    {
        auto now = mApp.getClock().now();
        auto timeout = getIOTimeout();
        auto stragglerTimeout =
            std::chrono::seconds(mApp.getConfig().PEER_STRAGGLER_TIMEOUT);
        if (((now - mLastRead) >= timeout) && ((now - mLastWrite) >= timeout))
        {
            CLOG(WARNING, "Overlay") << "idle timeout for peer " << toString();
            mTimeoutIdle.Mark();
            drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
        }
        else if (((now - mLastEmpty) >= stragglerTimeout))
        {
            CLOG(WARNING, "Overlay")
                << "peer " << toString() << " straggling (cannot keep up)";
            mTimeoutStraggler.Mark();
            drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
        }
        else
        {
            startIdleTimer();
        }
    }
}

void
Peer::sendAuth()
{
    EssaBlockChainMessage msg;
    msg.type(AUTH);
    sendMessage(msg);
}

std::string
Peer::toString()
{
    return mAddress.toString();
}

void
Peer::drop(ErrorCode err, std::string const& msg, DropMode dropMode)
{
    EssaBlockChainMessage m;
    m.type(ERROR_MSG);
    m.error().code = err;
    m.error().msg = msg;
    sendMessage(m);
    // note: this used to be a post which caused delays in stopping
    // to process read messages.
    // this will try to send all data from send queue if the error is ERR_LOAD
    // - it sends list of peers
    drop(dropMode);
}

void
Peer::connectHandler(asio::error_code const& error)
{
    if (error)
    {
        CLOG(WARNING, "Overlay")
            << " connectHandler error: " << error.message();
        drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
    }
    else
    {
        CLOG(DEBUG, "Overlay") << "connected " << toString();
        connected();
        mState = CONNECTED;
        sendHello();
    }
}

void
Peer::sendDontHave(MessageType type, uint256 const& itemID)
{
    EssaBlockChainMessage msg;
    msg.type(DONT_HAVE);
    msg.dontHave().reqHash = itemID;
    msg.dontHave().type = type;

    sendMessage(msg);
}

void
Peer::sendSCPQuorumSet(SCPQuorumSetPtr qSet)
{
    EssaBlockChainMessage msg;
    msg.type(SCP_QUORUMSET);
    msg.qSet() = *qSet;

    sendMessage(msg);
}
void
Peer::sendGetTxSet(uint256 const& setID)
{
    EssaBlockChainMessage newMsg;
    newMsg.type(GET_TX_SET);
    newMsg.txSetHash() = setID;

    sendMessage(newMsg);
}
void
Peer::sendGetQuorumSet(uint256 const& setID)
{
    if (Logging::logTrace("Overlay"))
        CLOG(TRACE, "Overlay") << "Get quorum set: " << hexAbbrev(setID);

    EssaBlockChainMessage newMsg;
    newMsg.type(GET_SCP_QUORUMSET);
    newMsg.qSetHash() = setID;

    sendMessage(newMsg);
}

void
Peer::sendGetPeers()
{
    CLOG(TRACE, "Overlay") << "Get peers";

    EssaBlockChainMessage newMsg;
    newMsg.type(GET_PEERS);

    sendMessage(newMsg);
}

void
Peer::sendGetScpState(uint32 ledgerSeq)
{
    CLOG(TRACE, "Overlay") << "Get SCP State for " << ledgerSeq;

    EssaBlockChainMessage newMsg;
    newMsg.type(GET_SCP_STATE);
    newMsg.getSCPLedgerSeq() = ledgerSeq;

    sendMessage(newMsg);
}

void
Peer::sendPeers()
{
    EssaBlockChainMessage newMsg;
    newMsg.type(PEERS);
    uint32 maxPeerCount = std::min<uint32>(50, newMsg.peers().max_size());

    // send top peers we know about
    auto peers = mApp.getOverlayManager().getPeerManager().getPeersToSend(
        maxPeerCount, mAddress);
    assert(peers.size() <= maxPeerCount);

    newMsg.peers().reserve(peers.size());
    for (auto const& address : peers)
    {
        newMsg.peers().push_back(toXdr(address));
    }
    sendMessage(newMsg);
}

static std::string
msgSummary(EssaBlockChainMessage const& msg)
{
    switch (msg.type())
    {
    case ERROR_MSG:
        return "ERROR";
    case HELLO:
        return "HELLO";
    case AUTH:
        return "AUTH";
    case DONT_HAVE:
        return "DONTHAVE";
    case GET_PEERS:
        return "GETPEERS";
    case PEERS:
        return "PEERS";

    case GET_TX_SET:
        return "GETTXSET";
    case TX_SET:
        return "TXSET";

    case TRANSACTION:
        return "TRANSACTION";

    case GET_SCP_QUORUMSET:
        return "GET_SCP_QSET";
    case SCP_QUORUMSET:
        return "SCP_QSET";
    case SCP_MESSAGE:
        switch (msg.envelope().statement.pledges.type())
        {
        case SCP_ST_PREPARE:
            return "SCP::PREPARE";
        case SCP_ST_CONFIRM:
            return "SCP::CONFIRM";
        case SCP_ST_EXTERNALIZE:
            return "SCP::EXTERNALIZE";
        case SCP_ST_NOMINATE:
            return "SCP::NOMINATE";
        }
    case GET_SCP_STATE:
        return "GET_SCP_STATE";
    }
    return "UNKNOWN";
}

void
Peer::sendMessage(EssaBlockChainMessage const& msg)
{
    if (Logging::logTrace("Overlay"))
        CLOG(TRACE, "Overlay")
            << "("
            << mApp.getConfig().toShortString(
                   mApp.getConfig().NODE_SEED.getPublicKey())
            << ") send: " << msgSummary(msg)
            << " to : " << mApp.getConfig().toShortString(mPeerID);

    switch (msg.type())
    {
    case ERROR_MSG:
        mSendErrorMeter.Mark();
        break;
    case HELLO:
        mSendHelloMeter.Mark();
        break;
    case AUTH:
        mSendAuthMeter.Mark();
        break;
    case DONT_HAVE:
        mSendDontHaveMeter.Mark();
        break;
    case GET_PEERS:
        mSendGetPeersMeter.Mark();
        break;
    case PEERS:
        mSendPeersMeter.Mark();
        break;
    case GET_TX_SET:
        mSendGetTxSetMeter.Mark();
        break;
    case TX_SET:
        mSendTxSetMeter.Mark();
        break;
    case TRANSACTION:
        mSendTransactionMeter.Mark();
        break;
    case GET_SCP_QUORUMSET:
        mSendGetSCPQuorumSetMeter.Mark();
        break;
    case SCP_QUORUMSET:
        mSendSCPQuorumSetMeter.Mark();
        break;
    case SCP_MESSAGE:
        mSendSCPMessageSetMeter.Mark();
        break;
    case GET_SCP_STATE:
        mSendGetSCPStateMeter.Mark();
        break;
    };

    AuthenticatedMessage amsg;
    amsg.v0().message = msg;
    if (msg.type() != HELLO && msg.type() != ERROR_MSG)
    {
        amsg.v0().sequence = mSendMacSeq;
        amsg.v0().mac =
            hmacSha256(mSendMacKey, xdr::xdr_to_opaque(mSendMacSeq, msg));
        ++mSendMacSeq;
    }
    xdr::msg_ptr xdrBytes(xdr::xdr_to_msg(amsg));
    this->sendMessage(std::move(xdrBytes));
}

void
Peer::recvMessage(xdr::msg_ptr const& msg)
{
    if (shouldAbort())
    {
        return;
    }

    LoadManager::PeerContext loadCtx(mApp, mPeerID);

    CLOG(TRACE, "Overlay") << "received xdr::msg_ptr";
    try
    {
        AuthenticatedMessage am;
        xdr::xdr_from_msg(msg, am);
        recvMessage(am);
    }
    catch (xdr::xdr_runtime_error& e)
    {
        CLOG(ERROR, "Overlay") << "received corrupt xdr::msg_ptr " << e.what();
        drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }
}

bool
Peer::isConnected() const
{
    return mState != CONNECTING && mState != CLOSING;
}

bool
Peer::isAuthenticated() const
{
    return mState == GOT_AUTH;
}

bool
Peer::shouldAbort() const
{
    return (mState == CLOSING) || mApp.getOverlayManager().isShuttingDown();
}

void
Peer::recvMessage(AuthenticatedMessage const& msg)
{
    if (shouldAbort())
    {
        return;
    }

    if (mState >= GOT_HELLO && msg.v0().message.type() != ERROR_MSG)
    {
        if (msg.v0().sequence != mRecvMacSeq)
        {
            CLOG(ERROR, "Overlay") << "Unexpected message-auth sequence";
            ++mRecvMacSeq;
            drop(ERR_AUTH, "unexpected auth sequence",
                 Peer::DropMode::IGNORE_WRITE_QUEUE);
            return;
        }

        if (!hmacSha256Verify(
                msg.v0().mac, mRecvMacKey,
                xdr::xdr_to_opaque(msg.v0().sequence, msg.v0().message)))
        {
            CLOG(ERROR, "Overlay") << "Message-auth check failed";
            ++mRecvMacSeq;
            drop(ERR_AUTH, "unexpected MAC",
                 Peer::DropMode::IGNORE_WRITE_QUEUE);
            return;
        }
        ++mRecvMacSeq;
    }
    recvMessage(msg.v0().message);
}

void
Peer::recvMessage(EssaBlockChainMessage const& essaMsg)
{
    if (shouldAbort())
    {
        return;
    }

    if (Logging::logTrace("Overlay"))
        CLOG(TRACE, "Overlay")
            << "("
            << mApp.getConfig().toShortString(
                   mApp.getConfig().NODE_SEED.getPublicKey())
            << ") recv: " << msgSummary(essaMsg)
            << " from:" << mApp.getConfig().toShortString(mPeerID);

    if (!isAuthenticated() && (essaMsg.type() != HELLO) &&
        (essaMsg.type() != AUTH) && (essaMsg.type() != ERROR_MSG))
    {
        CLOG(WARNING, "Overlay")
            << "recv: " << essaMsg.type() << " before completed handshake";
        drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }

    assert(isAuthenticated() || essaMsg.type() == HELLO ||
           essaMsg.type() == AUTH || essaMsg.type() == ERROR_MSG);

    switch (essaMsg.type())
    {
    case ERROR_MSG:
    {
        auto t = mRecvErrorTimer.TimeScope();
        recvError(essaMsg);
    }
    break;

    case HELLO:
    {
        auto t = mRecvHelloTimer.TimeScope();
        this->recvHello(essaMsg.hello());
    }
    break;

    case AUTH:
    {
        auto t = mRecvAuthTimer.TimeScope();
        this->recvAuth(essaMsg);
    }
    break;

    case DONT_HAVE:
    {
        auto t = mRecvDontHaveTimer.TimeScope();
        recvDontHave(essaMsg);
    }
    break;

    case GET_PEERS:
    {
        auto t = mRecvGetPeersTimer.TimeScope();
        recvGetPeers(essaMsg);
    }
    break;

    case PEERS:
    {
        auto t = mRecvPeersTimer.TimeScope();
        recvPeers(essaMsg);
    }
    break;

    case GET_TX_SET:
    {
        auto t = mRecvGetTxSetTimer.TimeScope();
        recvGetTxSet(essaMsg);
    }
    break;

    case TX_SET:
    {
        auto t = mRecvTxSetTimer.TimeScope();
        recvTxSet(essaMsg);
    }
    break;

    case TRANSACTION:
    {
        auto t = mRecvTransactionTimer.TimeScope();
        recvTransaction(essaMsg);
    }
    break;

    case GET_SCP_QUORUMSET:
    {
        auto t = mRecvGetSCPQuorumSetTimer.TimeScope();
        recvGetSCPQuorumSet(essaMsg);
    }
    break;

    case SCP_QUORUMSET:
    {
        auto t = mRecvSCPQuorumSetTimer.TimeScope();
        recvSCPQuorumSet(essaMsg);
    }
    break;

    case SCP_MESSAGE:
    {
        auto t = mRecvSCPMessageTimer.TimeScope();
        recvSCPMessage(essaMsg);
    }
    break;

    case GET_SCP_STATE:
    {
        auto t = mRecvGetSCPStateTimer.TimeScope();
        recvGetSCPState(essaMsg);
    }
    break;
    }
}

void
Peer::recvDontHave(EssaBlockChainMessage const& msg)
{
    mApp.getHerder().peerDoesntHave(msg.dontHave().type, msg.dontHave().reqHash,
                                    shared_from_this());
}

void
Peer::recvGetTxSet(EssaBlockChainMessage const& msg)
{
    auto self = shared_from_this();
    if (auto txSet = mApp.getHerder().getTxSet(msg.txSetHash()))
    {
        EssaBlockChainMessage newMsg;
        newMsg.type(TX_SET);
        txSet->toXDR(newMsg.txSet());

        self->sendMessage(newMsg);
    }
    else
    {
        sendDontHave(TX_SET, msg.txSetHash());
    }
}

void
Peer::recvTxSet(EssaBlockChainMessage const& msg)
{
    TxSetFrame frame(mApp.getNetworkID(), msg.txSet());
    mApp.getHerder().recvTxSet(frame.getContentsHash(), frame);
}

void
Peer::recvTransaction(EssaBlockChainMessage const& msg)
{
    TransactionFramePtr transaction = TransactionFrame::makeTransactionFromWire(
        mApp.getNetworkID(), msg.transaction());
    if (transaction)
    {
        // add it to our current set
        // and make sure it is valid
        auto recvRes = mApp.getHerder().recvTransaction(transaction);

        if (recvRes == Herder::TX_STATUS_PENDING ||
            recvRes == Herder::TX_STATUS_DUPLICATE)
        {
            // record that this peer sent us this transaction
            mApp.getOverlayManager().recvFloodedMsg(msg, shared_from_this());

            if (recvRes == Herder::TX_STATUS_PENDING)
            {
                // if it's a new transaction, broadcast it
                mApp.getOverlayManager().broadcastMessage(msg);
            }
        }
    }
}

void
Peer::recvGetSCPQuorumSet(EssaBlockChainMessage const& msg)
{
    SCPQuorumSetPtr qset = mApp.getHerder().getQSet(msg.qSetHash());

    if (qset)
    {
        sendSCPQuorumSet(qset);
    }
    else
    {
        if (Logging::logTrace("Overlay"))
            CLOG(TRACE, "Overlay")
                << "No quorum set: " << hexAbbrev(msg.qSetHash());
        sendDontHave(SCP_QUORUMSET, msg.qSetHash());
        // do we want to ask other people for it?
    }
}
void
Peer::recvSCPQuorumSet(EssaBlockChainMessage const& msg)
{
    Hash hash = sha256(xdr::xdr_to_opaque(msg.qSet()));
    mApp.getHerder().recvSCPQuorumSet(hash, msg.qSet());
}

void
Peer::recvSCPMessage(EssaBlockChainMessage const& msg)
{
    SCPEnvelope const& envelope = msg.envelope();
    if (Logging::logTrace("Overlay"))
        CLOG(TRACE, "Overlay")
            << "recvSCPMessage node: "
            << mApp.getConfig().toShortString(msg.envelope().statement.nodeID);

    auto type = msg.envelope().statement.pledges.type();
    auto t = (type == SCP_ST_PREPARE
                  ? mRecvSCPPrepareTimer.TimeScope()
                  : (type == SCP_ST_CONFIRM
                         ? mRecvSCPConfirmTimer.TimeScope()
                         : (type == SCP_ST_EXTERNALIZE
                                ? mRecvSCPExternalizeTimer.TimeScope()
                                : (mRecvSCPNominateTimer.TimeScope()))));

    auto res = mApp.getHerder().recvSCPEnvelope(envelope);
    if (res != Herder::ENVELOPE_STATUS_DISCARDED)
    {
        mApp.getOverlayManager().recvFloodedMsg(msg, shared_from_this());
    }
}

void
Peer::recvGetSCPState(EssaBlockChainMessage const& msg)
{
    uint32 seq = msg.getSCPLedgerSeq();
    CLOG(TRACE, "Overlay") << "get SCP State " << seq;
    mApp.getHerder().sendSCPStateToPeer(seq, shared_from_this());
}

void
Peer::recvError(EssaBlockChainMessage const& msg)
{
    std::string codeStr = "UNKNOWN";
    switch (msg.error().code)
    {
    case ERR_MISC:
        codeStr = "ERR_MISC";
        break;
    case ERR_DATA:
        codeStr = "ERR_DATA";
        break;
    case ERR_CONF:
        codeStr = "ERR_CONF";
        break;
    case ERR_AUTH:
        codeStr = "ERR_AUTH";
        break;
    case ERR_LOAD:
        codeStr = "ERR_LOAD";
        break;
    default:
        break;
    }
    CLOG(WARNING, "Overlay")
        << "Received error (" << codeStr << "): " << msg.error().msg;
    drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
}

void
Peer::updatePeerRecordAfterEcho()
{
    assert(!getAddress().isEmpty());

    auto type = mApp.getOverlayManager().isPreferred(this)
                    ? PeerManager::TypeUpdate::SET_PREFERRED
                    : mRole == WE_CALLED_REMOTE
                          ? PeerManager::TypeUpdate::SET_OUTBOUND
                          : PeerManager::TypeUpdate::REMOVE_PREFERRED;
    mApp.getOverlayManager().getPeerManager().update(getAddress(), type);
}

void
Peer::updatePeerRecordAfterAuthentication()
{
    assert(!getAddress().isEmpty());

    if (mRole == WE_CALLED_REMOTE)
    {
        mApp.getOverlayManager().getPeerManager().update(
            getAddress(), PeerManager::BackOffUpdate::RESET);
    }

    CLOG(INFO, "Overlay") << "successful handshake with "
                          << mApp.getConfig().toShortString(mPeerID) << "@"
                          << getAddress().toString();
}

void
Peer::recvHello(Hello const& elo)
{
    if (mState >= GOT_HELLO)
    {
        CLOG(ERROR, "Overlay") << "received unexpected HELLO";
        drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }

    auto& peerAuth = mApp.getOverlayManager().getPeerAuth();
    if (!peerAuth.verifyRemoteAuthCert(elo.peerID, elo.cert))
    {
        CLOG(ERROR, "Overlay") << "failed to verify remote peer auth cert";
        drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }

    if (mApp.getBanManager().isBanned(elo.peerID))
    {
        CLOG(ERROR, "Overlay") << "Node is banned";
        drop(Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }

    mRemoteOverlayMinVersion = elo.overlayMinVersion;
    mRemoteOverlayVersion = elo.overlayVersion;
    mRemoteVersion = elo.versionStr;
    mPeerID = elo.peerID;
    mRecvNonce = elo.nonce;
    mSendMacSeq = 0;
    mRecvMacSeq = 0;
    mSendMacKey = peerAuth.getSendingMacKey(elo.cert.pubkey, mSendNonce,
                                            mRecvNonce, mRole);
    mRecvMacKey = peerAuth.getReceivingMacKey(elo.cert.pubkey, mSendNonce,
                                              mRecvNonce, mRole);

    mState = GOT_HELLO;
    CLOG(DEBUG, "Overlay") << "recvHello from " << toString();

    auto dropMode = Peer::DropMode::IGNORE_WRITE_QUEUE;
    if (mRole == REMOTE_CALLED_US)
    {
        // Send a HELLO back, even if it's going to be followed
        // immediately by ERROR, because ERROR is an authenticated
        // message type and the caller won't decode it right if
        // still waiting for an unauthenticated HELLO.
        sendHello();
        dropMode = Peer::DropMode::FLUSH_WRITE_QUEUE;
    }

    if (mRemoteOverlayMinVersion > mRemoteOverlayVersion ||
        mRemoteOverlayVersion < mApp.getConfig().OVERLAY_PROTOCOL_MIN_VERSION ||
        mRemoteOverlayMinVersion > mApp.getConfig().OVERLAY_PROTOCOL_VERSION)
    {
        CLOG(ERROR, "Overlay") << "connection from peer with incompatible "
                                  "overlay protocol version";
        CLOG(DEBUG, "Overlay")
            << "Protocol = [" << mRemoteOverlayMinVersion << ","
            << mRemoteOverlayVersion << "] expected: ["
            << mApp.getConfig().OVERLAY_PROTOCOL_VERSION << ","
            << mApp.getConfig().OVERLAY_PROTOCOL_VERSION << "]";
        drop(ERR_CONF, "wrong protocol version", dropMode);
        return;
    }

    if (elo.peerID == mApp.getConfig().NODE_SEED.getPublicKey())
    {
        CLOG(WARNING, "Overlay") << "connecting to self";
        drop(ERR_CONF, "connecting to self", dropMode);
        return;
    }

    if (elo.networkID != mApp.getNetworkID())
    {
        CLOG(WARNING, "Overlay")
            << "connection from peer with different NetworkID";
        CLOG(DEBUG, "Overlay")
            << "NetworkID = " << hexAbbrev(elo.networkID)
            << " expected: " << hexAbbrev(mApp.getNetworkID());
        drop(ERR_CONF, "wrong network passphrase", dropMode);
        return;
    }

    auto ip = getIP();
    if (elo.listeningPort <= 0 || elo.listeningPort > UINT16_MAX || ip.empty())
    {
        CLOG(WARNING, "Overlay") << "bad address in recvHello";
        drop(ERR_CONF, "bad address", Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }

    mAddress =
        PeerBareAddress{ip, static_cast<unsigned short>(elo.listeningPort)};
    updatePeerRecordAfterEcho();

    auto const& authenticated =
        mApp.getOverlayManager().getAuthenticatedPeers();
    auto authenticatedIt = authenticated.find(mPeerID);
    // no need to self-check here as this one cannot be in authenticated yet
    if (authenticatedIt != std::end(authenticated))
    {
        if (&(authenticatedIt->second->mPeerID) != &mPeerID)
        {
            CLOG(WARNING, "Overlay")
                << "connection from already-connected peerID "
                << mApp.getConfig().toShortString(mPeerID);
            drop(ERR_CONF, "connecting already-connected peer", dropMode);
            return;
        }
    }

    for (auto const& p : mApp.getOverlayManager().getPendingPeers())
    {
        if (&(p->mPeerID) == &mPeerID)
        {
            continue;
        }
        if (p->getPeerID() == mPeerID)
        {
            CLOG(WARNING, "Overlay")
                << "connection from already-connected peerID "
                << mApp.getConfig().toShortString(mPeerID);
            drop(ERR_CONF, "connecting already-connected peer", dropMode);
            return;
        }
    }

    if (mRole == WE_CALLED_REMOTE)
    {
        sendAuth();
    }
}

void
Peer::recvAuth(EssaBlockChainMessage const& msg)
{
    if (mState != GOT_HELLO)
    {
        CLOG(INFO, "Overlay") << "Unexpected AUTH message before HELLO";
        drop(ERR_MISC, "out-of-order AUTH message",
             Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }

    if (isAuthenticated())
    {
        CLOG(INFO, "Overlay") << "Unexpected AUTH message";
        drop(ERR_MISC, "out-of-order AUTH message",
             Peer::DropMode::IGNORE_WRITE_QUEUE);
        return;
    }

    mState = GOT_AUTH;

    if (mRole == REMOTE_CALLED_US)
    {
        sendAuth();
        sendPeers();
    }

    updatePeerRecordAfterAuthentication();

    auto self = shared_from_this();
    if (!mApp.getOverlayManager().acceptAuthenticatedPeer(self))
    {
        CLOG(WARNING, "Overlay") << "New peer rejected, all slots taken";
        drop(ERR_LOAD, "peer rejected", Peer::DropMode::FLUSH_WRITE_QUEUE);
        return;
    }

    // ask for SCP state if not synced
    // this requests data for slots lcl-window ... latest consensus (if
    // possible) we need to ask for older slots in case other peers need
    // messages we didn't need ourself
    auto low = mApp.getLedgerManager().getLastClosedLedgerNum() + 1;
    if (low > Herder::MAX_SLOTS_TO_REMEMBER)
    {
        low -= Herder::MAX_SLOTS_TO_REMEMBER;
    }
    else
    {
        low = 1;
    }
    sendGetScpState(low);
}

void
Peer::recvGetPeers(EssaBlockChainMessage const& msg)
{
    sendPeers();
}

void
Peer::recvPeers(EssaBlockChainMessage const& msg)
{
    for (auto const& peer : msg.peers())
    {
        if (peer.port == 0 || peer.port > UINT16_MAX)
        {
            CLOG(WARNING, "Overlay")
                << "ignoring received peer with bad port " << peer.port;
            continue;
        }
        if (peer.ip.type() == IPv6)
        {
            CLOG(WARNING, "Overlay") << "ignoring received IPv6 address"
                                     << " (not yet supported)";
            continue;
        }

        assert(peer.ip.type() == IPv4);
        auto address = PeerBareAddress{peer};

        if (address.isPrivate())
        {
            CLOG(WARNING, "Overlay")
                << "ignoring received private address " << address.toString();
        }
        else if (address == PeerBareAddress{getAddress().getIP(),
                                            mApp.getConfig().PEER_PORT})
        {
            CLOG(WARNING, "Overlay")
                << "ignoring received self-address " << address.toString();
        }
        else if (address.isLocalhost() &&
                 !mApp.getConfig().ALLOW_LOCALHOST_FOR_TESTING)
        {
            CLOG(WARNING, "Overlay") << "ignoring received localhost";
        }
        else
        {
            // don't use peer.numFailures here as we may have better luck
            // (and we don't want to poison our failure count)
            mApp.getOverlayManager().getPeerManager().ensureExists(address);
        }
    }
}
}
