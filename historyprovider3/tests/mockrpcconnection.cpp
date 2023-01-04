#include "mockrpcconnection.h"
#include "src/historyapp.h"

shv::chainpack::RpcValue make_sub_params(const std::string& path, const std::string& method)
{
    return shv::chainpack::RpcValue::fromCpon(R"({"method":")" + method + R"(","path":")" + path + R"("}")");
}

shv::chainpack::RpcResponse MockRpcConnection::createResponse(const shv::chainpack::RpcValue& result)
{
    shv::chainpack::RpcResponse res;
    res.setRequestId(m_messageQueue.head().requestId());
    res.setResult(result);
    mockInfo() << "Sending response:" << res.toPrettyString();
    m_messageQueue.dequeue();
    return res;
}

void MockRpcConnection::doRespond(const shv::chainpack::RpcValue& result)
{
    emit rpcMessageReceived(createResponse(result));
}

void MockRpcConnection::doRespondInEventLoop(const shv::chainpack::RpcValue& result)
{
    QTimer::singleShot(0, [this, response = createResponse(result)] {emit rpcMessageReceived(response);});
}

shv::chainpack::RpcRequest MockRpcConnection::createRequest(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params)
{
    shv::chainpack::RpcRequest req;
    req.setRequestId(nextRequestId());
    req.setAccessGrant(shv::chainpack::Rpc::ROLE_ADMIN);
    req.setShvPath(path);
    req.setParams(params);
    req.setMethod(method);
    mockInfo() << "Sending request:" << req.toPrettyString();
    return req;
}

void MockRpcConnection::doRequest(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params)
{
    emit rpcMessageReceived(createRequest(path, method, params));
}

void MockRpcConnection::doRequestInEventLoop(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params)
{
    QTimer::singleShot(0, [this, request = createRequest(path, method, params)] {emit rpcMessageReceived(request);});
}

shv::chainpack::RpcSignal MockRpcConnection::createNotification(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params)
{
    shv::chainpack::RpcSignal sig;
    sig.setShvPath(path);
    sig.setMethod(method);
    sig.setParams(params);
    mockInfo() << "Sending signal:" << sig.toPrettyString();
    return sig;
}

void MockRpcConnection::doNotify(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params)
{
    emit rpcMessageReceived(createNotification(path, method, params));
}

void MockRpcConnection::doNotifyInEventLoop(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params)
{
    QTimer::singleShot(0, [this, notification = createNotification(path, method, params)] {emit rpcMessageReceived(notification);});
}

void MockRpcConnection::advanceTest()
{
    if (m_testDriverState == m_testDriver.end()) {
        CAPTURE("Client sent unexpected message after test end");
        CAPTURE(m_messageQueue.head());
        REQUIRE(false);
    }

    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
        m_timeoutTimer->deleteLater();
        m_timeoutTimer = nullptr;
    }

    m_coroRunning = true;
    ++m_testDriverState;
    m_coroRunning = false;
    if (m_testDriverState != m_testDriver.end()) {
        // I also need to dereference the value to trigger any unhandled exceptions.
        *m_testDriverState;
    }

    if (m_testDriverState == m_testDriver.end()) {
        // We'll wait a bit before ending to make sure the client isn't sending more messages.
        QTimer::singleShot(100, [] {
            HistoryApp::instance()->exit();
        });
    }
}

MockRpcConnection::MockRpcConnection()
    : shv::iotqt::rpc::DeviceConnection(nullptr)
    , m_testDriver(driver())
    , m_testDriverState(m_testDriver.begin())
{
    connect(this, &MockRpcConnection::handleNextMessage, this, &MockRpcConnection::advanceTest, Qt::QueuedConnection);
}

void MockRpcConnection::open()
{
    mockInfo() << "Client connected";
    m_connectionState.state = State::BrokerConnected;
    emit brokerConnectedChanged(true);
}

void MockRpcConnection::setBrokerConnected(bool state)
{
    mockInfo() << "Setting new broker connected state:" << state;
    if (state) {
        m_connectionState.state = State::BrokerConnected;
    } else {
        m_connectionState.state = State::ConnectionError;
    }
    QTimer::singleShot(0, [this, state] {emit brokerConnectedChanged(state);});
}

void MockRpcConnection::sendMessage(const shv::chainpack::RpcMessage& rpc_msg)
{
    auto msg_type =
        rpc_msg.isRequest() ? "message:" :
        rpc_msg.isResponse() ? "response:" :
        rpc_msg.isSignal() ? "signal:" :
        "<unknown message type>:";
    mockInfo() << "Got client" << msg_type << rpc_msg.toPrettyString();

    if (m_coroRunning) {
        throw std::logic_error("A client send a message while the test driver was running."
                " This can lead to unexpected behavior, because the test driver resumes on messages from the client and you can't resume the driver while it's already running.");
    }

    // We skip the cmdlog signals, but let others through.
    if (rpc_msg.isSignal() && rpc_msg.method() == "cmdlog") {
        return;
    }

    m_messageQueue.enqueue(rpc_msg);

    emit handleNextMessage();
}
