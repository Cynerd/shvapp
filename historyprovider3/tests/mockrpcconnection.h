#pragma once
#include <shv/coreqt/log.h>
#include <shv/iotqt/rpc/deviceconnection.h>
#include <doctest/doctest.h>

#include <QTimer>
#include <QQueue>

#define mockDebug() shvCDebug("MockRpcConnection")
#define mockInfo() shvCInfo("MockRpcConnection")
#define mockError() shvCError("MockRpcConnection")

enum CallNext {
	Yes,
	No
};

class MockRpcConnection;

template <typename Callable>
void enqueue(QQueue<std::function<CallNext(MockRpcConnection*)>>& q, Callable&& callable)
{
	if constexpr (std::is_same<std::invoke_result_t<Callable, MockRpcConnection*>, void>()) {
		q.enqueue([callable] (MockRpcConnection* mock) {
			callable(mock);
			return CallNext::No;
		});
		return;
	} else {
		q.enqueue(callable);
	}
}

class MockRpcConnection : public shv::iotqt::rpc::DeviceConnection {
	Q_OBJECT

	using Super = shv::iotqt::rpc::DeviceConnection;

private:
	shv::chainpack::RpcResponse createResponse(const shv::chainpack::RpcValue& result);
	shv::chainpack::RpcResponse createErrorResponse(const std::string& error_msg);
	shv::chainpack::RpcRequest createRequest(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params);
	shv::chainpack::RpcSignal createNotification(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params);

	bool m_driverRunning = false;
	QQueue<std::function<CallNext(MockRpcConnection*)>> m_testDriver;

public:
	void advanceTest();
	void doRespond(const shv::chainpack::RpcValue& result);
	void doRespondInEventLoop(const shv::chainpack::RpcValue& result);
	void doRespondErrorInEventLoop(const std::string& error_msg);
	void doRequest(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params);
	void doRequestInEventLoop(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params);
	void doNotify(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params);
	void doNotifyInEventLoop(const std::string& path, const std::string& method, const shv::chainpack::RpcValue& params);
	void setBrokerConnected(bool state);

	MockRpcConnection(const QQueue<std::function<CallNext(MockRpcConnection*)>>& test_driver);
	void open() override;
	Q_SIGNAL void handleNextMessage();
	void sendRpcMessage(const shv::chainpack::RpcMessage& rpc_msg) override;

	QQueue<shv::chainpack::RpcMessage> m_messageQueue;
	QTimer* m_timeoutTimer = nullptr;
};

const auto DRIVER_TIMEOUT = 3000;

shv::chainpack::RpcValue make_sub_params(const std::string& path, const std::string& method);

#define SETUP_TIMEOUT { \
	mock->m_timeoutTimer = new QTimer(); \
	QObject::connect(mock->m_timeoutTimer, &QTimer::timeout, [] {throw std::runtime_error("The test timed out while waiting for a message from client.");}); \
	mock->m_timeoutTimer->start(DRIVER_TIMEOUT); \
}

#define REQUEST_YIELD(path, method, ...) { \
	shv::chainpack::RpcValue params; \
	__VA_OPT__(params = (__VA_ARGS__)); \
	mock->doRequestInEventLoop((path), (method), (params)); \
	SETUP_TIMEOUT; \
	return; \
}

#define REQUEST(path, method, ...) { \
	shv::chainpack::RpcValue params; \
	__VA_OPT__(params = (__VA_ARGS__)); \
	mock->doRequest((path), (method), (params)); \
}

#define NOTIFY_YIELD(path, method, params) { \
	mock->doNotifyInEventLoop((path), (method), (params)); \
	SETUP_TIMEOUT; \
	return; \
}

#define NOTIFY(path, method, params) { \
	mock->doNotify((path), (method), (params)); \
}

#define RESPOND(result) { \
	mock->doRespond(result); \
}

#define RESPOND_YIELD(result, ...) { \
	mock->doRespondInEventLoop(result); \
	SETUP_TIMEOUT; \
	__VA_OPT__(return __VA_ARGS__); \
	return CallNext::No; \
}

#define RESPOND_ERROR_YIELD(error_msg, ...) { \
	mock->doRespondErrorInEventLoop(error_msg); \
	SETUP_TIMEOUT; \
	__VA_OPT__(return __VA_ARGS__); \
	return CallNext::No; \
}

#define EXPECT_REQUEST(pathStr, methodStr, ...) { \
	REQUIRE(!mock->m_messageQueue.empty()); \
	CAPTURE(mock->m_messageQueue.head()); \
	REQUIRE(mock->m_messageQueue.head().isRequest()); \
	REQUIRE(mock->m_messageQueue.head().shvPath().asString() == (pathStr)); \
	REQUIRE(mock->m_messageQueue.head().method().asString() == (methodStr)); \
	__VA_OPT__(REQUIRE(shv::chainpack::RpcRequest(mock->m_messageQueue.head()).params() == (__VA_ARGS__));) \
}

#define EXPECT_RESPONSE(expectedResult) { \
	REQUIRE(!mock->m_messageQueue.empty()); \
	CAPTURE(mock->m_messageQueue.head()); \
	REQUIRE(mock->m_messageQueue.head().isResponse()); \
	REQUIRE(shv::chainpack::RpcResponse(mock->m_messageQueue.head()).result() == (expectedResult)); \
	mock->m_messageQueue.dequeue(); \
}

#define EXPECT_SIGNAL(pathStr, methodStr, ...) { \
	REQUIRE(!mock->m_messageQueue.empty()); \
	CAPTURE(mock->m_messageQueue.head()); \
	REQUIRE(mock->m_messageQueue.head().isSignal()); \
	REQUIRE(mock->m_messageQueue.head().shvPath().asString() == (pathStr)); \
	REQUIRE(mock->m_messageQueue.head().method().asString() == (methodStr)); \
	__VA_OPT__(REQUIRE(shv::chainpack::RpcRequest(mock->m_messageQueue.head()).params() == (__VA_ARGS__));) \
	mock->m_messageQueue.dequeue(); \
}

#define EXPECT_ERROR(expectedMsg) { \
	REQUIRE(!mock->m_messageQueue.empty()); \
	CAPTURE(mock->m_messageQueue.head()); \
	REQUIRE(mock->m_messageQueue.head().isResponse()); \
	REQUIRE(shv::chainpack::RpcResponse(mock->m_messageQueue.head()).errorString() == (expectedMsg)); \
	mock->m_messageQueue.dequeue(); \
}

#define EXPECT_SUBSCRIPTION(path, method) { \
	EXPECT_REQUEST(".broker/app", "subscribe", make_sub_params(path, method)); \
	RESPOND(true); \
}

#define ENABLE_MAP_FILE_API(path) { \
	EXPECT_REQUEST(path, "dir", "sha1"); \
	RESPOND_YIELD(shv::chainpack::RpcValue{nullptr}); \
}

#define EXPECT_SUBSCRIPTION_YIELD(path, method) { \
	EXPECT_REQUEST(".broker/app", "subscribe", make_sub_params(path, method)); \
	RESPOND_YIELD(true); \
}

#define SEND_SITES_YIELD_AND_HANDLE_SUB(sitesStr, subPath) { \
	EXPECT_REQUEST("sites", "getSites", RpcValue()); \
	RESPOND_YIELD(sitesStr); \
	EXPECT_SUBSCRIPTION(subPath, "chng"); \
}

#define SEND_SITES_YIELD(sitesStr) { \
	EXPECT_REQUEST("sites", "getSites", RpcValue()); \
	RESPOND_YIELD(sitesStr); \
}

#define DISABLE_TYPEINFO(site) { \
	EXPECT_REQUEST(shv::core::utils::joinPath("sites", (site), "_files"), "ls", RpcValue()); \
	RESPOND_YIELD(RpcValue::List{}); \
}

#define ENABLE_TYPEINFO(site) { \
	EXPECT_REQUEST(shv::core::utils::joinPath("sites", (site), "_files"), "ls", RpcValue()); \
	RESPOND_YIELD(RpcValue::List{std::string{"typeInfo.cpon"}}); \
}

#define SEND_TYPEINFO_YIELD(site, typeinfoStr) { \
	EXPECT_REQUEST(shv::core::utils::joinPath("sites", (site), "_files/typeInfo.cpon"), "read", RpcValue()); \
	RESPOND_YIELD(typeinfoStr); \
}

#define SEND_TYPEINFO(site, typeinfoStr) { \
	EXPECT_REQUEST(shv::core::utils::joinPath("sites", (site), "_files/typeInfo.cpon"), "read", RpcValue()); \
	RESPOND(typeinfoStr); \
}

#define SEND_SITES(sitesStr) { \
	EXPECT_REQUEST("sites", "getSites", RpcValue()); \
	RESPOND(sitesStr); \
}

#define DRIVER_WAIT(msec) { \
	QTimer::singleShot(msec, mock, [mock] { mock->advanceTest(); }); \
	return; \
}
