#include "brokerapp.h"
#include "rpc/tcpserver.h"
#include "rpc/serverconnection.h"

#include <shv/iotqt/shvnode.h>
#include <shv/iotqt/shvnodetree.h>
#include <shv/coreqt/log.h>

#include <shv/core/string.h>
#include <shv/core/utils.h>
#include <shv/chainpack/rpcmessage.h>

#include <QSocketNotifier>
#include <QTimer>

#include <ctime>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#endif


namespace cp = shv::chainpack;
//#define logOpcuaReceive qfCInfo("OpcuaReceive")

#ifdef Q_OS_UNIX
int BrokerApp::m_sigTermFd[2];
#endif

//static constexpr int SQL_RECONNECT_INTERVAL = 3000;
ConnectionNode::ConnectionNode(rpc::ServerConnection *connection, QObject *parent)
 : Super(parent)
 , m_connection(connection)
{
	connect(connection, &rpc::ServerConnection::destroyed, [this]() {
		this->setParentNode(nullptr);
		delete this;
	});
}

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
	connect(this, &BrokerApp::sqlServerConnected, this, &BrokerApp::onSqlServerConnected);
	/*
	m_sqlConnectionWatchDog = new QTimer(this);
	connect(m_sqlConnectionWatchDog, SIGNAL(timeout()), this, SLOT(reconnectSqlServer()));
	m_sqlConnectionWatchDog->start(SQL_RECONNECT_INTERVAL);
	*/
	m_deviceTree = new shv::iotqt::ShvNodeTree(this);
	m_deviceTree->mkdir("test");

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
*/
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

std::string BrokerApp::mountPointForDevice(const shv::chainpack::RpcValue &device_id)
{
	Q_UNUSED(device_id)
	return std::string();
}

void BrokerApp::onClientLogin(int connection_id)
{
	rpc::ServerConnection *conn = tcpServer()->connectionById(connection_id);
	if(!conn)
		SHV_EXCEPTION("Cannot find connection for ID: " + std::to_string(connection_id));
	std::string mount_point = conn->device().toMap().value("mount").toString();
	if(mount_point.empty()) {
		shv::chainpack::RpcValue device_id = conn->device().toMap().value("id");
		mount_point = mountPointForDevice(device_id);
		if(mount_point.empty())
			SHV_EXCEPTION("Cannot find mount point for device: " + device_id.toStdString());
	}
	if(mount_point.empty())
		SHV_EXCEPTION("Mount point is empty.");
	ConnectionNode *nd = new ConnectionNode(conn);
	if(!m_deviceTree->mount(mount_point, nd))
		SHV_EXCEPTION("Cannot mount connection to device tree, connection id: " + std::to_string(connection_id));
}

void BrokerApp::onRpcDataReceived(cp::RpcValue::MetaData &&meta, std::string &&data)
{
	const std::string shv_path = cp::RpcMessage::shvPath(meta).toString();
	std::string path_rest;
	ConnectionNode *nd = qobject_cast<ConnectionNode *>(m_deviceTree->cd(shv_path, &path_rest));
	if(nd) {
		cp::RpcMessage::setShvPath(meta, path_rest.empty()? cp::RpcValue(): cp::RpcValue(path_rest));
		rpc::ServerConnection *conn2 = nd->connection();
		conn2->sendRawData(std::move(meta), std::move(data));
	}
	else {
		shvWarning() << "Device tree path not found:" << shv_path;
	}
}

