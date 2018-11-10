#include "serverconnection.h"

#include "../brokerapp.h"

#include <shv/chainpack/cponwriter.h>
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
	connect(this, &ServerConnection::socketConnectedChanged, [this](bool is_connected) {
		if(!is_connected) {
			shvInfo() << "Socket disconnected, deleting connection:" << connectionId();
			deleteLater();
		}
	});
}

ServerConnection::~ServerConnection()
{
	//rpc::ServerConnectionshvWarning() << "destroying" << this;
}

const shv::chainpack::RpcValue::Map& ServerConnection::deviceOptions() const
{
	return connectionOptions().value(cp::Rpc::KEY_DEVICE).toMap();
}

const shv::chainpack::RpcValue::Map& ServerConnection::brokerOptions() const
{
	return connectionOptions().value(cp::Rpc::KEY_BROKER).toMap();
}

shv::chainpack::RpcValue ServerConnection::deviceId() const
{
	return deviceOptions().value(cp::Rpc::KEY_DEVICE_ID);
}

void ServerConnection::addMountPoint(const std::string &mp)
{
	m_mountPoints.push_back(mp);
}

void ServerConnection::setIdleWatchDogTimeOut(unsigned sec)
{
	if(sec == 0) {
		shvInfo() << "connection ID:" << connectionId() << "switching idle watch dog timeout OFF";
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

std::string ServerConnection::passwordHash(PasswordHashType type, const std::string &user)
{
	/*
	const std::map<std::string, std::string> passwds {
		{"iot", "lub42DUB"},
		{"elviz", "brch3900PRD"},
		{"revitest", "lautrhovno271828"},
	};
	*/
	BrokerApp *app = BrokerApp::instance();
	const shv::chainpack::RpcValue::Map &user_def = app->usersConfig().toMap().value(user).toMap();
	std::string pass = user_def.value("password").toString();
	std::string pass_hash = user_def.value("passwordHashType").toString();
	if(type == PasswordHashType::Plain && pass_hash == "plain") {
		return pass;
	}
	if(type == PasswordHashType::Sha1 && pass_hash == "sha1") {
		return pass;
	}
	if(type == PasswordHashType::Sha1 && pass_hash == "plain") {
		QCryptographicHash hash(QCryptographicHash::Algorithm::Sha1);
		hash.addData(pass.data(), pass.size());
		QByteArray sha1 = hash.result().toHex();
		//shvWarning() << user << pass << sha1;
		return std::string(sha1.constData(), sha1.length());
		return pass;
	}
	return std::string();
}

std::string ServerConnection::dataToCpon(shv::chainpack::Rpc::ProtocolType protocol_type, const shv::chainpack::RpcValue::MetaData &md, const std::string &data, size_t start_pos)
{
	shv::chainpack::RpcValue rpc_val;
	if(data.size() - start_pos < 256) {
		rpc_val = shv::chainpack::RpcDriver::decodeData(protocol_type, data, start_pos);
	}
	else {
		rpc_val = " ... " + std::to_string(data.size() - start_pos) + " bytes of data ... ";
	}
	rpc_val.setMetaData(shv::chainpack::RpcValue::MetaData(md));
	std::ostringstream out;
	{
		cp::CponWriterOptions opts;
		opts.setTranslateIds(BrokerApp::instance()->cliOptions()->isMetaTypeExplicit());
		cp::CponWriter wr(out, opts);
		wr << rpc_val;
	}
	return out.str();
}

void ServerConnection::sendMessage(const shv::chainpack::RpcMessage &rpc_msg)
{
	logRpcMsg() << SND_LOG_ARROW
				<< "client id:" << connectionId()
				<< "protocol_type:" << (int)protocolType() << shv::chainpack::Rpc::protocolTypeToString(protocolType())
				<< rpc_msg.toCpon();
	Super::sendMessage(rpc_msg);
}

void ServerConnection::sendRawData(const shv::chainpack::RpcValue::MetaData &meta_data, std::string &&data)
{
	logRpcMsg() << SND_LOG_ARROW
				<< "client id:" << connectionId()
				<< "protocol_type:" << (int)protocolType() << shv::chainpack::Rpc::protocolTypeToString(protocolType())
				<< dataToCpon(shv::chainpack::RpcMessage::protocolType(meta_data), meta_data, data, 0);
	Super::sendRawData(meta_data, std::move(data));
}

void ServerConnection::onRpcDataReceived(shv::chainpack::Rpc::ProtocolType protocol_type, shv::chainpack::RpcValue::MetaData &&md, const std::string &data, size_t start_pos, size_t data_len)
{
	logRpcMsg() << RCV_LOG_ARROW
				<< "client id:" << connectionId()
				<< "protocol_type:" << (int)protocol_type << shv::chainpack::Rpc::protocolTypeToString(protocol_type)
				<< dataToCpon(protocol_type, md, data, start_pos);
	try {
		if(isInitPhase()) {
			Super::onRpcDataReceived(protocol_type, std::move(md), data, start_pos, data_len);
			return;
		}
		if(m_idleWatchDogTimer)
			m_idleWatchDogTimer->start();
		cp::RpcMessage::setProtocolType(md, protocol_type);
		if(cp::RpcMessage::isRegisterRevCallerIds(md))
			cp::RpcMessage::pushRevCallerId(md, connectionId());
		std::string msg_data(data, start_pos, data_len);
		BrokerApp::instance()->onRpcDataReceived(connectionId(), std::move(md), std::move(msg_data));
	}
	catch (std::exception &e) {
		shvError() << e.what();
	}
}

bool ServerConnection::checkPassword(const shv::chainpack::RpcValue::Map &login)
{
	return Super::checkPassword(login);
}

shv::chainpack::RpcValue ServerConnection::login(const shv::chainpack::RpcValue &auth_params)
{
	cp::RpcValue ret = Super::login(auth_params);
	if(!ret.isValid())
		return cp::RpcValue();
	//cp::RpcValue::Map login_resp = ret.toMap();

	const shv::chainpack::RpcValue::Map &opts = connectionOptions();
	shv::chainpack::RpcValue::UInt t = opts.value(cp::Rpc::OPT_IDLE_WD_TIMEOUT).toUInt();
	setIdleWatchDogTimeOut(t);
	BrokerApp::instance()->onClientLogin(connectionId());

	//login_resp[cp::Rpc::KEY_CLIENT_ID] = connectionId();
	//if(!mountPoint().empty())
	//	login_resp[cp::Rpc::KEY_MOUT_POINT] = mountPoint();
	return ret;
}

void ServerConnection::createSubscription(const std::string &rel_path, const std::string &method)
{
	std::string abs_path = rel_path;
	if(Subscription::isRelativePath(abs_path)) {
		const std::vector<std::string> &mps = mountPoints();
		if(mps.empty())
			SHV_EXCEPTION("Cannot subscribe relative path on unmounted device.");
		if(mps.size() > 1)
			SHV_EXCEPTION("Cannot subscribe relative path on device mounted to more than single node.");
		abs_path = Subscription::toAbsolutePath(mps[0], rel_path);
	}
	Subscription su(abs_path, rel_path, method);
	auto it = std::find(m_subscriptions.begin(), m_subscriptions.end(), su);
	if(it == m_subscriptions.end()) {
		m_subscriptions.push_back(su);
		std::sort(m_subscriptions.begin(), m_subscriptions.end());
	}
	else {
		*it = su;
	}
}

int ServerConnection::isSubscribed(const std::string &path, const std::string &method) const
{
	shv::core::StringView shv_path(path);
	while(shv_path.length() && shv_path[0] == '/')
		shv_path = shv_path.mid(1);
	for (size_t i = 0; i < m_subscriptions.size(); ++i) {
		const Subscription &subs = m_subscriptions[i];
		if(subs.match(shv_path, method))
			return i;
	}
	return -1;
}

std::string ServerConnection::Subscription::toRelativePath(const std::string &abs_path, bool &changed) const
{
	if(relativePath.empty()) {
		changed = false;
		return abs_path;
	}
	std::string ret = relativePath + abs_path.substr(absolutePath.size());
	changed = true;
	return ret;
}

static const std::string DDOT("../");

bool ServerConnection::Subscription::isRelativePath(const std::string &path)
{
	shv::core::StringView p(path);
	return p.startsWith(DDOT);
}

std::string ServerConnection::Subscription::toAbsolutePath(const std::string &mount_point, const std::string &rel_path)
{
	shv::core::StringView p(rel_path);
	size_t ddot_cnt = 0;
	while(p.startsWith(DDOT)) {
		ddot_cnt++;
		p = p.mid(DDOT.size());
	}
	//while(p.length() && p[0] == '/')
	//	p = p.mid(1);
	std::string abs_path;
	if(ddot_cnt > 0 && !mount_point.empty()) {
		std::vector<shv::core::StringView> mpl = shv::iotqt::node::ShvNode::splitPath(mount_point);
		if(mpl.size() >= ddot_cnt) {
			mpl.resize(mpl.size() - ddot_cnt);
			abs_path = shv::core::StringView::join(mpl, "/") + '/' + p.toString();
			return abs_path;
		}
	}
	return rel_path;
}

bool ServerConnection::Subscription::operator<(const ServerConnection::Subscription &o) const
{
	int i = absolutePath.compare(o.absolutePath);
	if(i == 0)
		return method < o.method;
	return (i < 0);
}

bool ServerConnection::Subscription::operator==(const ServerConnection::Subscription &o) const
{
	int i = absolutePath.compare(o.absolutePath);
	if(i == 0)
		return method == o.method;
	return false;
}

bool ServerConnection::Subscription::match(const shv::core::StringView &shv_path, const shv::core::StringView &shv_method) const
{
	//shvInfo() << pathPattern << ':' << method << "match" << shv_path.toString() << ':' << shv_method.toString();// << "==" << true;
	if(shv_path.startsWith(absolutePath)) {
		if(shv_path.length() == absolutePath.length())
			return (method.empty() || shv_method == method);
		if(shv_path.length() > absolutePath.length()
				&& (absolutePath.empty() || shv_path[absolutePath.length()] == '/'))
			return (method.empty() || shv_method == method);
	}
	return false;
}
/*
shv::chainpack::Rpc::AccessGrant ServerConnection::accessGrantForShvPath(const std::string &shv_path)
{
	shv::chainpack::Rpc::AccessGrant *pag = m_accessGrantCache.object(shv_path);
	if(!pag) {
		BrokerApp *app = BrokerApp::instance();
		shv::chainpack::Rpc::AccessGrant ag = app->accessGrantForShvPath(m_user, shv_path);
		m_accessGrantCache.insert(shv_path, new cp::Rpc::AccessGrant(ag));
		return ag;
	}
	return *pag;
}
*/
} // namespace rpc
