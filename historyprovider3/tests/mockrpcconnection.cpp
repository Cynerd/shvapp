#include "mockrpcconnection.h"
#include "src/historyapp.h"

#include <shv/iotqt/rpc/socket.h>


class MockSocket : public shv::iotqt::rpc::TcpSocket {
public:
	using shv::iotqt::rpc::TcpSocket::TcpSocket;
	QAbstractSocket::SocketState state() const override
	{
		return QAbstractSocket::ConnectedState;
	}
};

shv::chainpack::RpcValue make_sub_params(const std::string& path, const std::string& method)
{
	return shv::chainpack::RpcValue::fromCpon(R"({"method":")" + method + R"(","path":")" + path + R"(","source": ""})");
}

shv::chainpack::RpcValue make_read_response(const std::string& dummy_logfile, int offset, const std::optional<int>& size)
{
	auto blob = shv::chainpack::RpcValue(shv::chainpack::RpcValue::stringToBlob(dummy_logfile.substr(offset, size.value_or(std::string::npos))));
	blob.setMetaValue("size", blob.asBlob().size());
	blob.setMetaValue("offset", offset);
	return blob;
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

shv::chainpack::RpcResponse MockRpcConnection::createErrorResponse(const std::string& error_msg)
{
	shv::chainpack::RpcResponse res;
	res.setRequestId(m_messageQueue.head().requestId());
	res.setError(shv::chainpack::RpcError::createMethodCallExceptionError(error_msg));
	mockInfo() << "Sending error response:" << res.toPrettyString();
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

void MockRpcConnection::doRespondErrorInEventLoop(const std::string& result)
{
	QTimer::singleShot(0, [this, response = createErrorResponse(result)] {emit rpcMessageReceived(response);});
}

shv::chainpack::RpcRequest MockRpcConnection::createRequest(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params)
{
	shv::chainpack::RpcRequest req;
	req.setRequestId(nextRequestId());
	req.setAccessLevel(shv::chainpack::AccessLevel::Admin);
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
	if (m_testDriver.empty()) {
		CAPTURE("Client sent unexpected message after test end");
		CAPTURE(m_messageQueue.head());
		REQUIRE(false);
	}

	if (m_timeoutTimer) {
		m_timeoutTimer->stop();
		m_timeoutTimer->deleteLater();
		m_timeoutTimer = nullptr;
	}

	m_driverRunning = true;
	while(m_testDriver.head()(this) == CallNext::Yes) {
		m_testDriver.dequeue();
	}
	m_testDriver.dequeue();
	m_driverRunning = false;

	if (m_testDriver.empty()) {
		// We'll wait a bit before ending to make sure the client isn't sending more messages.
		QTimer::singleShot(100, [] {
			HistoryApp::exit();
		});
	}
}

MockRpcConnection::MockRpcConnection(const QQueue<std::function<CallNext(MockRpcConnection*)>>& test_driver)
	: shv::iotqt::rpc::DeviceConnection(nullptr)
	, m_testDriver(test_driver)
{
	setSocket(new MockSocket(new QTcpSocket(), this));
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
	QTimer::singleShot(0, this, [this, state] {emit brokerConnectedChanged(state);});
}

void MockRpcConnection::sendRpcMessage(const shv::chainpack::RpcMessage& rpc_msg)
{
	auto msg_type =
			rpc_msg.isRequest() ? "message:" :
								  rpc_msg.isResponse() ? "response:" :
														 rpc_msg.isSignal() ? "signal:" :
																			  "<unknown message type>:";
	mockInfo() << "Got client" << msg_type << rpc_msg.toPrettyString();

	if (m_driverRunning) {
		throw std::logic_error("A client send a message while the test driver was running."
							   " This can lead to unexpected behavior, because the test driver resumes on messages from the client and you can't resume the driver while it's already running.");
	}

	m_messageQueue.enqueue(rpc_msg);

	emit handleNextMessage();
}
