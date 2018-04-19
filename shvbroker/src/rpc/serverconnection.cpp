#include "serverconnection.h"

#include "../brokerapp.h"

#include <shv/coreqt/log.h>
#include <shv/core/stringview.h>

#include <QCryptographicHash>
#include <QTcpSocket>
#include <QTimer>

#define logRpcMsg() shvCDebug("RpcMsg")

namespace cp = shv::chainpack;

namespace rpc {

ServerConnection::ServerConnection(QTcpSocket *socket, QObject *parent)
	: Super(socket, parent)
{
	connect(socket, &QTcpSocket::disconnected, this, [this]() {
		shvInfo() << "Socket disconnected, deleting connection:" << connectionId();
		deleteLater();
	});
	connect(this, &ServerConnection::socketConnectedChanged, [this](bool is_connected) {
		if(is_connected) {
			m_helloReceived = m_loginReceived = false;
		}
	});
}

shv::chainpack::RpcValue ServerConnection::deviceId() const
{
	const shv::chainpack::RpcValue::Map &device = connectionOptions().value(cp::Rpc::TYPE_DEVICE).toMap();
	return device.value("id");
}

void ServerConnection::setIdleWatchDogTimeOut(unsigned sec)
{
	if(sec == 0) {
		shvInfo() << "connection ID:" << connectionId() << "switchong idle watch dog timeout OFF";
		if(m_idleWatchDogTimer) {
			delete m_idleWatchDogTimer;
			m_idleWatchDogTimer = nullptr;
		}
	}
	else {
		if(!m_idleWatchDogTimer) {
			m_idleWatchDogTimer = new QTimer(this);
			connect(m_idleWatchDogTimer, &QTimer::timeout, [this]() {
				shvError() << "Connection was idle for more than" << m_idleWatchDogTimer->interval()/1000 << "sec. It will be aborted.";
				this->abort();
			});
		}
		shvInfo() << "connection ID:" << connectionId() << "setting idle watch dog timeout to" << sec << "seconds";
		m_idleWatchDogTimer->start(sec * 1000);
	}
}

std::string ServerConnection::passwordHash(const std::string &user)
{
	if(user == "revitest")
		return std::string();

	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha1);
	std::string pass = (user == "iot")? "lub42DUB": user;
	hash.addData(pass.data(), pass.size());
	QByteArray sha1 = hash.result().toHex();
	//shvWarning() << user << pass << sha1;
	return std::string(sha1.constData(), sha1.length());
}

void ServerConnection::onRpcDataReceived(shv::chainpack::Rpc::ProtocolType protocol_version, shv::chainpack::RpcValue::MetaData &&md, const std::string &data, size_t start_pos, size_t data_len)
{
	logRpcMsg() << RCV_LOG_ARROW << md.toStdString() << shv::chainpack::Utils::toHexElided(data, start_pos, 100);
	try {
		if(isInitPhase()) {
			Super::onRpcDataReceived(protocol_version, std::move(md), data, start_pos, data_len);
			return;
		}
		if(m_idleWatchDogTimer)
			m_idleWatchDogTimer->start();
		cp::RpcMessage::setProtocolType(md, protocol_version);
		std::string msg_data(data, start_pos, data_len);
		BrokerApp::instance()->onRpcDataReceived(connectionId(), std::move(md), std::move(msg_data));
	}
	catch (std::exception &e) {
		shvError() << e.what();
	}
}

shv::chainpack::RpcValue ServerConnection::login(const shv::chainpack::RpcValue &auth_params)
{
	const cp::RpcValue::Map params = auth_params.toMap();
	const cp::RpcValue::Map login = params.value("login").toMap();

	m_user = login.value("user").toString();
	if(m_user.empty())
		return false;

	std::string password_hash = passwordHash(m_user);
	shvInfo() << "login - user:" << m_user << "password:" << password_hash;
	bool password_ok = password_hash.empty();
	if(!password_ok) {
		std::string nonce_sha1 = login.value("password").toString();
		std::string nonce = m_pendingAuthNonce + password_hash;
		//shvWarning() << m_pendingAuthNonce << "prd" << nonce;
		QCryptographicHash hash(QCryptographicHash::Algorithm::Sha1);
		hash.addData(nonce.data(), nonce.length());
		std::string sha1 = std::string(hash.result().toHex().constData());
		//shvInfo() << nonce_sha1 << "vs" << sha1;
		password_ok = (nonce_sha1 == sha1);
	}
	if(password_ok) {
		m_connectionType = params.value("type").toString();
		m_connectionOptions = params.value("options");
		const shv::chainpack::RpcValue::Map &opts = m_connectionOptions.toMap();
		shv::chainpack::RpcValue::UInt t = opts.value(cp::Rpc::OPT_IDLE_WD_TIMEOUT).toUInt();
		setIdleWatchDogTimeOut(t);
		BrokerApp::instance()->onClientLogin(connectionId());

		cp::RpcValue::Map login_resp;
		login_resp[cp::Rpc::KEY_CLIENT_ID] = connectionId();
		//login_resp[cp::Rpc::KEY_MOUT_POINT] = mountPoint();
		return login_resp;
	}
	return cp::RpcValue();
}

void ServerConnection::createSubscription(const std::string &path, const std::string &method)
{
	shv::core::StringView p(path);
	while(p.length() && p[0] == '/')
		p = p.mid(1);
	Subscription su(p.toString(), method);
	auto it = std::find(m_subscriptions.begin(), m_subscriptions.end(), su);
	if(it == m_subscriptions.end()) {
		m_subscriptions.push_back(su);
		std::sort(m_subscriptions.begin(), m_subscriptions.end());
	}
	else {
		*it = su;
	}
}

bool ServerConnection::isSubscribed(const std::string &path, const std::string &method) const
{
	shv::core::StringView shv_path(path);
	while(shv_path.length() && shv_path[0] == '/')
		shv_path = shv_path.mid(1);
	for (const Subscription &subs : m_subscriptions) {
		if(subs.match(shv_path, method))
			return true;
	}
	return false;
}

bool ServerConnection::Subscription::operator<(const ServerConnection::Subscription &o) const
{
	int i = pathPattern.compare(o.pathPattern);
	if(i == 0)
		return method < o.method;
	return (i < 0);
}

bool ServerConnection::Subscription::operator==(const ServerConnection::Subscription &o) const
{
	int i = pathPattern.compare(o.pathPattern);
	if(i == 0)
		return method == o.method;
	return false;
}

bool ServerConnection::Subscription::match(const shv::core::StringView &shv_path, const shv::core::StringView &shv_method) const
{
	//shvInfo() << pathPattern << ':' << method << "match" << shv_path.toString() << ':' << shv_method.toString();// << "==" << true;
	if(shv_path.startsWith(pathPattern)) {
		if(shv_path.length() == pathPattern.length())
			return (method.empty() || shv_method == method);
		if(shv_path.length() > pathPattern.length()
				&& (pathPattern.empty() || shv_path[pathPattern.length()] == '/'))
			return (method.empty() || shv_method == method);
	}
	return false;
}

} // namespace rpc
