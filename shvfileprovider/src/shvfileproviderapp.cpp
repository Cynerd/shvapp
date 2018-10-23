#include "shvfileproviderapp.h"
#include "appclioptions.h"
#include "sessionprocess.h"

#include <shv/iotqt/rpc/deviceconnection.h>
#include <shv/iotqt/rpc/tunnelhandle.h>
#include <shv/iotqt/node/shvnodetree.h>
#include <shv/iotqt/node/localfsnode.h>
#include <shv/coreqt/log.h>
#include <shv/chainpack/metamethod.h>

#include <shv/core/stringview.h>

#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <QtGlobal>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <unistd.h>
#include <errno.h>
#include <string.h>
#endif

#ifdef HANDLE_UNIX_SIGNALS
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cp = shv::chainpack;

static std::vector<cp::MetaMethod> meta_methods {
	{cp::Rpc::METH_DIR, cp::MetaMethod::Signature::RetParam, false},
	{cp::Rpc::METH_LS, cp::MetaMethod::Signature::RetParam, false},
	{cp::Rpc::METH_APP_NAME, cp::MetaMethod::Signature::RetVoid, false},
	{cp::Rpc::METH_CONNECTION_TYPE, cp::MetaMethod::Signature::RetVoid, false},
	{cp::Rpc::METH_DEVICE_ID, cp::MetaMethod::Signature::RetVoid, !cp::MetaMethod::IsSignal},
	{cp::Rpc::METH_HELP, cp::MetaMethod::Signature::RetParam, false},
	{cp::Rpc::METH_RUN_CMD, cp::MetaMethod::Signature::RetParam, false},
};

size_t AppRootNode::methodCount(const StringViewList &shv_path)
{
	return (!shv_path.empty()) ? meta_methods.size() : 0;
}

const shv::chainpack::MetaMethod *AppRootNode::metaMethod(const StringViewList &shv_path, size_t ix)
{
	if(shv_path.empty()) {
		if(meta_methods.size() <= ix)
			SHV_EXCEPTION("Invalid method index: " + std::to_string(ix) + " of: " + std::to_string(meta_methods.size()));
		return &(meta_methods[ix]);
	}
	return nullptr;
}

shv::chainpack::RpcValue AppRootNode::callMethod(const StringViewList &shv_path, const std::string &method, const shv::chainpack::RpcValue &params)
{
/*	if(shv_path.empty()) {
		if(method == cp::Rpc::METH_APP_NAME) {
			return QCoreApplication::instance()->applicationName().toStdString();
		}
		if(method == cp::Rpc::METH_CONNECTION_TYPE) {
			return ShvFileProviderApp::instance()->rpcConnection()->connectionType();
		}
		if(method == cp::Rpc::METH_HELP) {
			std::string meth = params.toString();
			if(meth == cp::Rpc::METH_RUN_CMD) {
				return 	"method: " + meth + "\n"
						"params: cmd_string OR [cmd_string, 1, 2]\n"
						"\tcmd_string: command with arguments to run\n"
						"\t1: remote process std_out will be set at this possition in returned list\n"
						"\t2: remote process std_err will be set at this possition in returned list\n"
						"Any param can be omited in params sa list, also param order is irrelevant, ie [1,\"ls\"] is valid param list."
						"return:\n"
						"\t* process exit_code if params are just cmd_string\n"
						"\t* list of process exit code and std_out and std_err on same possitions as they are in params list.\n"
						;
			}
			else {
				return "No help for method: " + meth;
			}
		}
	}
	return Super::callMethod(shv_path, method, params);*/
}

shv::chainpack::RpcValue AppRootNode::processRpcRequest(const shv::chainpack::RpcRequest &rq)
{
/*	if(rq.shvPath().toString().empty()) {
		if(rq.method() == cp::Rpc::METH_DEVICE_ID) {
			ShvFileProviderApp *app = ShvFileProviderApp::instance();
			cp::RpcValue::Map opts = app->rpcConnection()->connectionOptions().toMap();;
			cp::RpcValue::Map dev = opts.value(cp::Rpc::TYPE_DEVICE).toMap();
			//shvInfo() << dev[cp::Rpc::KEY_DEVICE_ID].toString();
			return dev.value(cp::Rpc::KEY_DEVICE_ID).toString();
		}
		if(rq.method() == cp::Rpc::METH_RUN_CMD) {
			ShvFileProviderApp *app = ShvFileProviderApp::instance();
			app->runCmd(rq);
			return cp::RpcValue();
		}
	}
	return Super::processRpcRequest(rq);*/
}

#ifdef HANDLE_UNIX_SIGNALS
namespace {
int sig_term_socket_ends[2];
QSocketNotifier *sig_term_socket_notifier = nullptr;
//struct sigaction* old_handlers[sizeof(handled_signals) / sizeof(int)];

void signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	Q_UNUSED(siginfo)
	Q_UNUSED(context)
	shvInfo() << "SIG:" << sig;
	char a = sig;
	::write(sig_term_socket_ends[0], &a, sizeof(a));
}

}

void ShvFileProviderApp::handleUnixSignal()
{
	sig_term_socket_notifier->setEnabled(false);
	char sig;
	::read(sig_term_socket_ends[1], &sig, sizeof(sig));

	shvInfo() << "SIG" << (int)sig << "catched.";
	emit aboutToTerminate((int)sig);

	sig_term_socket_notifier->setEnabled(true);
	shvInfo() << "Terminating application.";
	quit();
}

void ShvFileProviderApp::installUnixSignalHandlers()
{
	shvInfo() << "installing Unix signals handlers";
	{
		struct sigaction sig_act;
		memset (&sig_act, '\0', sizeof(sig_act));
		// Use the sa_sigaction field because the handles has two additional parameters
		sig_act.sa_sigaction = &signal_handler;
		// The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler.
		sig_act.sa_flags = SA_SIGINFO;
		const int handled_signals[] = {SIGTERM, SIGINT, SIGHUP, SIGUSR1, SIGUSR2};
		for(int s : handled_signals)
			if	(sigaction(s, &sig_act, 0) > 0)
				shvError() << "Couldn't register handler for signal:" << s;
	}
	if(::socketpair(AF_UNIX, SOCK_STREAM, 0, sig_term_socket_ends))
		qFatal("Couldn't create SIG_TERM socketpair");
	sig_term_socket_notifier = new QSocketNotifier(sig_term_socket_ends[1], QSocketNotifier::Read, this);
	connect(sig_term_socket_notifier, &QSocketNotifier::activated, this, &ShvFileProviderApp::handleUnixSignal);
	shvInfo() << "SIG_TERM handler installed OK";
}
#endif

ShvFileProviderApp::ShvFileProviderApp(int &argc, char **argv, AppCliOptions* cli_opts)
	: Super(argc, argv)
	, m_cliOptions(cli_opts)
{
#ifdef HANDLE_UNIX_SIGNALS
	installUnixSignalHandlers();
#endif
#ifdef Q_OS_UNIX
	if(0 != ::setpgid(0, 0))
		shvError() << "Error set process group ID:" << errno << ::strerror(errno);
#endif
	cp::RpcMessage::setMetaTypeExplicit(cli_opts->isMetaTypeExplicit());
	m_rpcConnection = new shv::iotqt::rpc::DeviceConnection(this);

	if(!cli_opts->user_isset()){
		cli_opts->setUser("iot");
	}

	if(!cli_opts->password_isset()){
		cli_opts->setPassword("lub42DUB");
	}

	m_rpcConnection->setCliOptions(cli_opts);

	connect(m_rpcConnection, &shv::iotqt::rpc::ClientConnection::brokerConnectedChanged, this, &ShvFileProviderApp::onBrokerConnectedChanged);
	connect(m_rpcConnection, &shv::iotqt::rpc::ClientConnection::rpcMessageReceived, this, &ShvFileProviderApp::onRpcMessageReceived);

	AppRootNode *root = new AppRootNode();
	m_shvTree = new shv::iotqt::node::ShvNodeTree(root, this);
	connect(m_shvTree->root(), &shv::iotqt::node::ShvRootNode::sendRpcMesage, m_rpcConnection, &shv::iotqt::rpc::ClientConnection::sendMessage);
	//m_shvTree->mkdir("sys/rproc");
	QString sys_fs_root_dir = cli_opts->sysFsRootDir();
	if(!sys_fs_root_dir.isEmpty() && QDir(sys_fs_root_dir).exists()) {
		const char *SYS_FS = "sys/fs";
		shvInfo() << "Exporting" << sys_fs_root_dir << "as" << SYS_FS << "node";
		shv::iotqt::node::LocalFSNode *fsn = new shv::iotqt::node::LocalFSNode(sys_fs_root_dir);
		m_shvTree->mount(SYS_FS, fsn);
	}

	if(cliOptions()->connStatusUpdateInterval() > 0) {
		QTimer *tm = new QTimer(this);
		connect(tm, &QTimer::timeout, this, &ShvFileProviderApp::updateConnStatusFile);
		tm->start(cliOptions()->connStatusUpdateInterval() * 1000);
	}

	QTimer::singleShot(0, m_rpcConnection, &shv::iotqt::rpc::ClientConnection::open);
}

ShvFileProviderApp::~ShvFileProviderApp()
{
	shvInfo() << "destroying shv agent application";
}

ShvFileProviderApp *ShvFileProviderApp::instance()
{
	return qobject_cast<ShvFileProviderApp *>(QCoreApplication::instance());
}

void ShvFileProviderApp::runCmd(const shv::chainpack::RpcRequest &rq)
{
/*	SessionProcess *proc = new SessionProcess(this);
	auto rq2 = rq;
	connect(proc, static_cast<void (SessionProcess::*)(int)>(&SessionProcess::finished), [this, proc, rq2](int) {
		cp::RpcValue result;
		if(rq2.params().isList()) {
			cp::RpcValue::List lst;
			for(auto p : rq2.params().toList()) {
				if(p.isString()) {
					lst.push_back(proc->exitCode());
				}
				else {
					int i = p.toInt();
					if(i == STDOUT_FILENO) {
						QByteArray ba = proc->readAllStandardOutput();
						lst.push_back(std::string(ba.constData(), ba.size()));
					}
					else if(i == STDERR_FILENO) {
						QByteArray ba = proc->readAllStandardError();
						lst.push_back(std::string(ba.constData(), ba.size()));
					}
					else {
						shvInfo() << "RunCmd: Invalid request parameter:" << p.toCpon();
						lst.push_back(nullptr);
					}
				}
			}
			result = lst;
		}
		else {
			QByteArray ba = proc->readAllStandardOutput();
			result = std::string(ba.constData(), ba.size());
		}
		cp::RpcResponse resp = cp::RpcResponse::forRequest(rq2);
		resp.setResult(result);
		m_rpcConnection->sendMessage(resp);
	});
	connect(proc, &QProcess::errorOccurred, [this, rq2](QProcess::ProcessError error) {
		shvInfo() << "RunCmd: Exec process error:" << error;
		if(error == QProcess::FailedToStart) {
			cp::RpcResponse resp = cp::RpcResponse::forRequest(rq2);
			resp.setError(cp::RpcResponse::Error::createMethodCallExceptionError("Failed to start process"));
			m_rpcConnection->sendMessage(resp);
		}
	});
	std::string cmd;
	if(rq.params().isList()) {
		for(auto p : rq.params().toList()) {
			if(p.isString()) {
				cmd = p.toString();
				break;
			}
		}
	}
	else {
		cmd = rq.params().toString();
	}
	proc->start(QString::fromStdString(cmd));*/
}

void ShvFileProviderApp::onBrokerConnectedChanged(bool is_connected)
{
	m_isBrokerConnected = is_connected;
}

void ShvFileProviderApp::onRpcMessageReceived(const shv::chainpack::RpcMessage &msg)
{
	shvLogFuncFrame() << msg.toCpon();
	if(msg.isRequest()) {
		cp::RpcRequest rq(msg);
		shvInfo() << "RPC request received:" << rq.toPrettyString();
		if(m_shvTree->root()) {
			m_shvTree->root()->handleRpcRequest(rq);
		}
	}
	else if(msg.isResponse()) {
		cp::RpcResponse rp(msg);
		shvInfo() << "RPC response received:" << rp.toPrettyString();
	}
	else if(msg.isNotify()) {
		cp::RpcNotify nt(msg);
		shvInfo() << "RPC notify received:" << nt.toPrettyString();
		/*
		if(nt.method() == cp::Rpc::NTF_VAL_CHANGED) {
			if(nt.shvPath() == "/test/shv/lublicator2/status") {
				shvInfo() << lublicatorStatusToString(nt.params().toUInt());
			}
		}
		*/
	}
}

void ShvFileProviderApp::updateConnStatusFile()
{
	QString fn = cliOptions()->connStatusFile();
	if(fn.isEmpty())
		return;
	QFile f(fn);
	QDir dir = QFileInfo(f).dir();
	if(!dir.mkpath(dir.absolutePath())) {
		shvError() << "Cannot create directory:" << dir.absolutePath();
		return;
	}
	if(f.open(QFile::WriteOnly)) {
		f.write(m_isBrokerConnected? "1": "0", 1);
	}
	else {
		shvError() << "Cannot write to connection statu file:" << fn;
	}
}


