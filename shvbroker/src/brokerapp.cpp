#include "brokerapp.h"
#include "shvclientnode.h"
#include "brokernode.h"
#include "subscriptionsnode.h"
#include "brokerconfigfilenode.h"
#include "clientdirnode.h"
#include "rpc/tcpserver.h"
#include "rpc/serverconnection.h"

#include <shv/iotqt/node/shvnode.h>
#include <shv/iotqt/node/shvnodetree.h>
#include <shv/iotqt/rpc/clientconnection.h>
#include <shv/coreqt/log.h>

#include <shv/core/string.h>
#include <shv/core/utils.h>
#include <shv/core/assert.h>
#include <shv/core/stringview.h>
#include <shv/chainpack/chainpackwriter.h>
#include <shv/chainpack/cponreader.h>
#include <shv/chainpack/rpcmessage.h>

#include <QFile>
#include <QSocketNotifier>
#include <QTimer>

#include <ctime>
#include <fstream>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

int BrokerApp::m_sigTermFd[2];
#endif

namespace cp = shv::chainpack;

//static constexpr int SQL_RECONNECT_INTERVAL = 3000;
BrokerApp::BrokerApp(int &argc, char **argv, AppCliOptions *cli_opts)
	: Super(argc, argv)
	, m_cliOptions(cli_opts)
{
	shvInfo() << "creating SHV BROKER application object ver." << versionString();
	std::srand(std::time(nullptr));
#ifdef Q_OS_UNIX
	//syslog (LOG_INFO, "Server started");
	installUnixSignalHandlers();
#endif

	cp::RpcMessage::setMetaTypeExplicit(cli_opts->isMetaTypeExplicit());

	//connect(this, &BrokerApp::sqlServerConnected, this, &BrokerApp::onSqlServerConnected);
	/*
	m_sqlConnectionWatchDog = new QTimer(this);
	connect(m_sqlConnectionWatchDog, SIGNAL(timeout()), this, SLOT(reconnectSqlServer()));
	m_sqlConnectionWatchDog->start(SQL_RECONNECT_INTERVAL);
	*/
	m_deviceTree = new shv::iotqt::node::ShvNodeTree(this);
	connect(m_deviceTree->root(), &shv::iotqt::node::ShvRootNode::sendRpcMesage, this, &BrokerApp::onRootNodeSendRpcMesage);
	BrokerNode *bn = new BrokerNode();
	m_deviceTree->mount(cp::Rpc::DIR_BROKER_APP, bn);
	m_deviceTree->mount(std::string(cp::Rpc::DIR_BROKER) + "/etc/acl", new EtcAclNode());

	QTimer::singleShot(0, this, &BrokerApp::lazyInit);
}

BrokerApp::~BrokerApp()
{
	shvInfo() << "Destroying SHV BROKER application object";
	//QF_SAFE_DELETE(m_tcpServer);
	//QF_SAFE_DELETE(m_sqlConnector);
}

QString BrokerApp::versionString() const
{
	return QCoreApplication::applicationVersion();
}

rpc::TcpServer *BrokerApp::tcpServer()
{
	if(!m_tcpServer)
		SHV_EXCEPTION("TCP server is NULL!");
	return m_tcpServer;
}

rpc::ServerConnection *BrokerApp::clientById(int client_id)
{
	return tcpServer()->connectionById(client_id);
}

void BrokerApp::invalidateConfigCache()
{
	m_fstab = cp::RpcValue();
	m_users = cp::RpcValue();
	m_grants = cp::RpcValue();
	m_paths = cp::RpcValue();
}

#ifdef Q_OS_UNIX
void BrokerApp::installUnixSignalHandlers()
{
	shvInfo() << "installing Unix signals handlers";
	{
		struct sigaction term;

		term.sa_handler = BrokerApp::sigTermHandler;
		sigemptyset(&term.sa_mask);
		term.sa_flags |= SA_RESTART;

		if(sigaction(SIGTERM, &term, 0) > 0)
			qFatal("Couldn't register SIG_TERM handler");
	}
	if(::socketpair(AF_UNIX, SOCK_STREAM, 0, m_sigTermFd))
		qFatal("Couldn't create SIG_TERM socketpair");
	m_snTerm = new QSocketNotifier(m_sigTermFd[1], QSocketNotifier::Read, this);
	connect(m_snTerm, &QSocketNotifier::activated, this, &BrokerApp::handleSigTerm);
	shvInfo() << "SIG_TERM handler installed OK";
}

void BrokerApp::sigTermHandler(int)
{
	shvInfo() << "SIG TERM";
	char a = 1;
	::write(m_sigTermFd[0], &a, sizeof(a));
}

void BrokerApp::handleSigTerm()
{
	m_snTerm->setEnabled(false);
	char tmp;
	::read(m_sigTermFd[1], &tmp, sizeof(tmp));

	shvInfo() << "SIG TERM catched, stopping server.";
	QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);

	m_snTerm->setEnabled(true);
}
#endif

/*
sql::SqlConnector *TheApp::sqlConnector()
{
	if(!m_sqlConnector || !m_sqlConnector->isOpen())
		QF_EXCEPTION("SQL server not connected!");
	return m_sqlConnector;
}

QString TheApp::serverProfile()
{
	return cliOptions()->profile();
}

QVariantMap TheApp::cliOptionsMap()
{
	return cliOptions()->values();
}

void TheApp::reconnectSqlServer()
{
	if(m_sqlConnector && m_sqlConnector->isOpen())
		return;
	QF_SAFE_DELETE(m_sqlConnector);
	m_sqlConnector = new sql::SqlConnector(this);
	connect(m_sqlConnector, SIGNAL(sqlServerError(QString)), this, SLOT(onSqlServerError(QString)), Qt::QueuedConnection);
	//connect(m_sqlConnection, SIGNAL(openChanged(bool)), this, SLOT(onSqlServerConnectedChanged(bool)), Qt::QueuedConnection);

	QString host = cliOptions()->sqlHost();
	int port = cliOptions()->sqlPort();
	m_sqlConnector->open(host, port);
	if (m_sqlConnector->isOpen()) {
		emit sqlServerConnected();
	}
}
void BrokerApp::onSqlServerError(const QString &err_mesg)
{
	Q_UNUSED(err_mesg)
	//SHV_SAFE_DELETE(m_sqlConnector);
	//m_sqlConnectionWatchDog->start(SQL_RECONNECT_INTERVAL);
}

void BrokerApp::onSqlServerConnected()
{
	//connect(depotModel(), &DepotModel::valueChangedWillBeEmitted, m_sqlConnector, &sql::SqlConnector::saveDepotModelJournal, Qt::UniqueConnection);
	//connect(this, &TheApp::opcValueWillBeSet, m_sqlConnector, &sql::SqlConnector::saveDepotModelJournal, Qt::UniqueConnection);
	//m_depotModel->setValue(QStringList() << QStringLiteral("server") << QStringLiteral("startTime"), QVariant::fromValue(QDateTime::currentDateTime()), !shv::core::Exception::Throw);
}
*/

void BrokerApp::startTcpServer()
{
	SHV_SAFE_DELETE(m_tcpServer);
	m_tcpServer = new rpc::TcpServer(this);
	if(!m_tcpServer->start(cliOptions()->serverPort())) {
		SHV_EXCEPTION("Cannot start TCP server!");
	}
}

void BrokerApp::lazyInit()
{
	//reconnectSqlServer();
	startTcpServer();
}

shv::chainpack::RpcValue BrokerApp::fstabConfig()
{
	return aclConfig("fstab", !shv::core::Exception::Throw);
}

shv::chainpack::RpcValue BrokerApp::aclConfig(const std::string &config_name, bool throw_exc)
{
	shv::chainpack::RpcValue *val = nullptr;
	if(config_name == "fstab")
		val = &m_fstab;
	else if(config_name == "users")
		val = &m_users;
	else if(config_name == "users")
		val = &m_grants;
	else if(config_name == "users")
		val = &m_paths;
	if(val) {
		if(!val->isValid())
			*val = loadAclConfig(config_name, throw_exc);
		if(!val->isValid())
			*val = cp::RpcValue::Map{}; /// will not be loaded next time
		return *val;
	}
	else {
		if(throw_exc)
			throw std::runtime_error("Cannot load config: " + config_name);
		else
			return cp::RpcValue();
	}
}

shv::chainpack::RpcValue BrokerApp::loadAclConfig(const std::string &config_name, bool throw_exc)
{
	QString fn = QString::fromStdString(config_name).trimmed();
	fn = cliOptions()->value("etc.acl." + fn).toString();
	if(fn.isEmpty()) {
		if(throw_exc)
			throw std::runtime_error("Invalid config name: " + config_name);
		else
			return cp::RpcValue();
	}
	if(!fn.startsWith('/'))
		fn = cliOptions()->configDir() + '/' + fn;
	shvDebug() << "broker config file:" << fn;
	QFile f(fn);
	if (!f.open(QFile::ReadOnly)) {
		if(throw_exc)
			throw std::runtime_error("Cannot open config file " + fn.toStdString() + " for reading");
		else
			return cp::RpcValue();
	}
	else {
		QByteArray ba = f.readAll();
		std::string cpon(ba.constData(), ba.size());
		std::string err;
		shv::chainpack::RpcValue rv = cp::RpcValue::fromCpon(cpon, throw_exc? nullptr: &err);
		invalidateConfigCache();
		return rv;
	}
}

bool BrokerApp::saveAclConfig(const std::string &config_name, const shv::chainpack::RpcValue &config, bool throw_exc)
{
	QString fn = QString::fromStdString(config_name).trimmed();
	fn = cliOptions()->value("etc.acl." + fn).toString();
	if(fn.isEmpty()) {
		if(throw_exc)
			throw std::runtime_error("config file name is empty.");
		else
			return false;
	}
	if(!fn.startsWith('/'))
		fn = cliOptions()->configDir() + '/' + fn;

	if(config.isMap()) {
		QFile f(fn);
		if (!f.open(QFile::WriteOnly)) {
			if(throw_exc)
				throw std::runtime_error("Cannot open config file " + fn.toStdString() + " for writing");
			else
				return false;
		}
		std::string cpon = config.toCpon("  ");
		f.write(cpon.data(), cpon.size());
		invalidateConfigCache();
		return true;
	}
	else {
		if(throw_exc)
			throw std::runtime_error("Cannot save invalid config to file " + fn.toStdString());
		else
			return false;
	}
}

std::string BrokerApp::mountPointForDevice(const shv::chainpack::RpcValue &device_id)
{
	shv::chainpack::RpcValue fstab = fstabConfig();
	const std::string dev_id = device_id.toString();
	std::string mount_point = m_fstab.toMap().value(dev_id).toString();
	return mount_point;
}

void BrokerApp::onClientLogin(int connection_id)
{
	rpc::ServerConnection *conn = tcpServer()->connectionById(connection_id);
	if(!conn)
		SHV_EXCEPTION("Cannot find connection for ID: " + std::to_string(connection_id));
	const shv::chainpack::RpcValue::Map &opts = conn->connectionOptions();

	shvInfo() << "Client login connection id:" << connection_id << "connection type:" << conn->connectionType();
	{
		std::string dir_mount_point = brokerClientDirPath(connection_id);
		{
			shv::iotqt::node::ShvNode *dir = m_deviceTree->cd(dir_mount_point);
			if(dir) {
				shvError() << "Client dir" << dir_mount_point << "exists already and will be deleted, this should never happen!";
				dir->setParentNode(nullptr);
				delete dir;
			}
		}
		shv::iotqt::node::ShvNode *clients_nd = m_deviceTree->mkdir(std::string(cp::Rpc::DIR_BROKER) + "/clients/");
		if(!clients_nd)
			SHV_EXCEPTION("Cannot create parent for ClientDirNode id: " + std::to_string(connection_id));
		ClientDirNode *client_dir_node = new ClientDirNode(connection_id, clients_nd);
		//shvWarning() << "path1:" << client_dir_node->shvPath();
		ShvClientNode *client_app_node = new ShvClientNode(conn, client_dir_node);
		client_app_node->setNodeId("app");
		//shvWarning() << "path2:" << client_app_node->shvPath();
		/*
		std::string app_mount_point = brokerClientAppPath(connection_id);
		shv::iotqt::node::ShvNode *curr_nd = m_deviceTree->cd(app_mount_point);
		ShvClientNode *curr_cli_nd = qobject_cast<ShvClientNode*>(curr_nd);
		if(curr_cli_nd) {
			shvWarning() << "The SHV client on" << app_mount_point << "exists already, this should never happen!";
			curr_cli_nd->connection()->abort();
			delete curr_cli_nd;
		}
		if(!m_deviceTree->mount(app_mount_point, cli_nd))
			SHV_EXCEPTION("Cannot mount connection to device tree, connection id: " + std::to_string(connection_id)
						  + " shv path: " + app_mount_point);
		*/
		// delete whole client tree, when client is destroyed
		connect(conn, &rpc::ServerConnection::destroyed, client_app_node->parentNode(), &ShvClientNode::deleteLater);
		/// do not send NTF_CONNECTED_CHANGED, exposing client ID maight be dangerous
		//this->sendNotifyToSubscribers(connection_id, mount_point, cp::Rpc::NTF_CONNECTED_CHANGED, true);
		//connect(conn, &rpc::ServerConnection::destroyed, this, [this, connection_id, mount_point]() {
		//	this->sendNotifyToSubscribers(connection_id, mount_point, cp::Rpc::NTF_CONNECTED_CHANGED, false);
		//});
		conn->setParent(client_app_node);
		{
			std::string mount_point = client_dir_node->shvPath() + "/subscriptions";
			SubscriptionsNode *nd = new SubscriptionsNode(conn);
			if(!m_deviceTree->mount(mount_point, nd))
				SHV_EXCEPTION("Cannot mount connection subscription list to device tree, connection id: " + std::to_string(connection_id)
							  + " shv path: " + mount_point);
		}
	}

	if(conn->connectionType() == cp::Rpc::TYPE_DEVICE) {
		std::string mount_point;
		shv::chainpack::RpcValue device_id = conn->deviceId();
		if(device_id.isValid())
			mount_point = mountPointForDevice(device_id);
		if(mount_point.empty()) {
			const shv::chainpack::RpcValue::Map &device = opts.value(cp::Rpc::TYPE_DEVICE).toMap();
			mount_point = device.value(cp::Rpc::KEY_MOUT_POINT).toString();
			std::vector<shv::core::StringView> path = shv::core::StringView(mount_point).split('/');
			if(path.size() && !(path[0] == "test")) {
				shvWarning() << "Mount point can be explicitly specified to test/ dir only, dev id:" << device_id.toCpon();
				mount_point.clear();
			}
		}
		if(mount_point.empty()) {
			//mount_point = DIR_BROKER + "/mountError/" + std::to_string(connection_id);
			//shvWarning() << "Device will be mounted to" << mount_point << ", dev id:" << device_id.toCpon();
			shvWarning() << "cannot find mount point for device id:" << device_id.toCpon() << "connection id:" << connection_id;
		}
		if(!mount_point.empty()) {
			ShvClientNode *cli_nd = qobject_cast<ShvClientNode*>(m_deviceTree->cd(mount_point));
			if(cli_nd) {
				/*
				shvWarning() << "The mount point" << mount_point << "exists already";
				if(cli_nd->connection()->deviceId() == device_id) {
					shvWarning() << "The same device ID will be remounted:" << device_id.toCpon();
					delete cli_nd;
				}
				*/
				cli_nd->addConnection(conn);
			}
			else {
				cli_nd = new ShvClientNode(conn);
				if(!m_deviceTree->mount(mount_point, cli_nd))
					SHV_EXCEPTION("Cannot mount connection to device tree, connection id: " + std::to_string(connection_id));
			}
			mount_point = cli_nd->shvPath();
			shvInfo() << "device id:" << device_id.toCpon() << " mounted on:" << mount_point;
			/// overwrite client default mount point
			conn->addMountPoint(mount_point);
			connect(conn, &rpc::ServerConnection::destroyed, this, [this, connection_id, mount_point]() {
				shvInfo() << "server connection destroyed";
				//this->sendNotifyToSubscribers(connection_id, mount_point, cp::Rpc::NTF_DISCONNECTED, cp::RpcValue());
				this->sendNotifyToSubscribers(connection_id, mount_point, cp::Rpc::NTF_MOUNTED_CHANGED, false);
			});
			connect(cli_nd, &ShvClientNode::destroyed, cli_nd->parentNode(), &shv::iotqt::node::ShvNode::deleteDanglingPath, static_cast<Qt::ConnectionType>(Qt::UniqueConnection | Qt::QueuedConnection));
			//sendNotifyToSubscribers(connection_id, mount_point, cp::Rpc::NTF_CONNECTED, cp::RpcValue());
			sendNotifyToSubscribers(connection_id, mount_point, cp::Rpc::NTF_MOUNTED_CHANGED, true);
		}
	}

	//shvInfo() << m_deviceTree->dumpTree();
}

void BrokerApp::onRpcDataReceived(int connection_id, cp::RpcValue::MetaData &&meta, std::string &&data)
{
	if(cp::RpcMessage::isRequest(meta)) {
		shvDebug() << "REQUEST conn id:" << connection_id << meta.toStdString();
		rpc::ServerConnection *conn = tcpServer()->connectionById(connection_id);
		std::string shv_path = cp::RpcMessage::shvPath(meta).toString();
		if(conn) {
			if(rpc::ServerConnection::Subscription::isRelativePath(shv_path)) {
				const std::vector<std::string> &mps = conn->mountPoints();
				if(mps.empty())
					SHV_EXCEPTION("Cannot call method on relative path for unmounted device.");
				if(mps.size() > 1)
					SHV_EXCEPTION("Cannot call method on relative path for device mounted to more than single node.");
				shv_path = rpc::ServerConnection::Subscription::toAbsolutePath(mps[0], shv_path);
			}
			cp::RpcMessage::setShvPath(meta, shv_path);
		}
		cp::RpcMessage::pushCallerId(meta, connection_id);
		if(m_deviceTree->root()) {
			m_deviceTree->root()->handleRawRpcRequest(std::move(meta), std::move(data));
		}
		else {
			std::string err_msg = "Device tree root node is NULL";
			shvWarning() << err_msg;
			if(cp::RpcMessage::isRequest(meta)) {
				rpc::ServerConnection *conn = tcpServer()->connectionById(connection_id);
				if(conn) {
					shv::chainpack::RpcResponse rsp = cp::RpcResponse::forRequest(meta);
					rsp.setError(cp::RpcResponse::Error::create(
									 cp::RpcResponse::Error::MethodCallException
									 , err_msg));
					conn->sendMessage(rsp);
				}
			}
		}
	}
	else if(cp::RpcMessage::isResponse(meta)) {
		shvDebug() << "RESPONSE conn id:" << connection_id << meta.toStdString();
		cp::RpcValue::Int caller_id = cp::RpcMessage::popCallerId(meta);
		if(caller_id > 0) {
			rpc::ServerConnection *conn = tcpServer()->connectionById(caller_id);
			if(conn) {
				conn->sendRawData(std::move(meta), std::move(data));
			}
			else {
				shvWarning() << "Got RPC response for not-exists connection, may be it was closed meanwhile. Connection id:" << caller_id;
			}
		}
		else {
			shvError() << "Got RPC response without src connection specified, throwing message away." << meta.toStdString();
		}
	}
	else if(cp::RpcMessage::isNotify(meta)) {
		shvDebug() << "NOTIFY:" << meta.toStdString() << "from:" << connection_id;
		rpc::ServerConnection *conn = tcpServer()->connectionById(connection_id);
		if(conn) {
			for(const std::string &mp : conn->mountPoints()) {
				std::string full_shv_path = shv::core::Utils::joinPath(mp, cp::RpcMessage::shvPath(meta).toString());
				if(!full_shv_path.empty()) {
					cp::RpcMessage::setShvPath(meta, full_shv_path);
					sendNotifyToSubscribers(connection_id, meta, data);
				}
			}
		}
	}
}

void BrokerApp::onRootNodeSendRpcMesage(const shv::chainpack::RpcMessage &msg)
{
	if(msg.isResponse()) {
		cp::RpcResponse resp(msg);
		shv::chainpack::RpcValue::Int connection_id = resp.popCallerId();
		rpc::ServerConnection *conn = tcpServer()->connectionById(connection_id);
		if(conn)
			conn->sendMessage(resp);
		else
			shvError() << "Cannot find connection for ID:" << connection_id;
		return;
	}
	shvError() << "Send message not implemented.";// << msg.toCpon();
}

void BrokerApp::sendNotifyToSubscribers(int sender_connection_id, const shv::chainpack::RpcValue::MetaData &meta_data, const std::string &data)
{
	// send it to all clients for now
	for(int id : tcpServer()->connectionIds()) {
		if(id == sender_connection_id)
			continue;
		rpc::ServerConnection *conn = tcpServer()->connectionById(id);
		if(conn && conn->isConnectedAndLoggedIn()) {
			const cp::RpcValue shv_path = cp::RpcMessage::shvPath(meta_data);
			const cp::RpcValue method = cp::RpcMessage::method(meta_data);
			int subs_ix = conn->isSubscribed(shv_path.toString(), method.toString());
			if(subs_ix >= 0) {
				shvDebug() << "\t broadcasting to connection id:" << id;
				const rpc::ServerConnection::Subscription &subs = conn->subscriptionAt((size_t)subs_ix);
				bool changed;
				std::string new_path = subs.toRelativePath(shv_path.toString(), changed);
				if(changed) {
					shv::chainpack::RpcValue::MetaData md2(meta_data);
					cp::RpcMessage::setShvPath(md2, new_path);
					conn->sendRawData(md2, std::string(data));
				}
				else {
					conn->sendRawData(meta_data, std::string(data));
				}
			}
		}
	}
}

std::string BrokerApp::brokerClientDirPath(int client_id)
{
	return std::string(cp::Rpc::DIR_BROKER) + "/clients/" + std::to_string(client_id);
}

std::string BrokerApp::brokerClientAppPath(int client_id)
{
	return brokerClientDirPath(client_id) + "/app";
}

void BrokerApp::sendNotifyToSubscribers(int sender_connection_id, const std::string &shv_path, const std::string &method, const shv::chainpack::RpcValue &params)
{
	cp::RpcNotify ntf;
	ntf.setShvPath(shv_path);
	ntf.setMethod(method);
	ntf.setParams(params);
	// send it to all clients for now
	for(int id : tcpServer()->connectionIds()) {
		if(id == sender_connection_id)
			continue;
		rpc::ServerConnection *conn = tcpServer()->connectionById(id);
		if(conn && conn->isConnectedAndLoggedIn()) {
			int subs_ix = conn->isSubscribed(shv_path, method);
			if(subs_ix >= 0) {
				shvDebug() << "\t broadcasting to connection id:" << id;
				const rpc::ServerConnection::Subscription &subs = conn->subscriptionAt((size_t)subs_ix);
				bool changed;
				std::string new_path = subs.toRelativePath(shv_path, changed);
				if(changed)
					ntf.setShvPath(new_path);
				conn->sendMessage(ntf);
			}
		}
	}
}

void BrokerApp::createSubscription(int client_id, const std::string &path, const std::string &method)
{
	rpc::ServerConnection *conn = tcpServer()->connectionById(client_id);
	if(!conn)
		SHV_EXCEPTION("Connot create subscription, client doesn't exist.");
	conn->createSubscription(path, method);
}


